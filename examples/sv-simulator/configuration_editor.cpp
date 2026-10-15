#include "configuration_editor.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace
{
QVariantMap map(const boost::json::value &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(boost::json::serialize(value)))
        .object()
        .toVariantMap();
}
} // namespace

std::shared_ptr<sv::client::Client> ConfigurationEditor::client() const
{
    const auto result = client_.lock();
    if (!result || !connected_)
    {
        throw std::runtime_error("server_not_connected");
    }
    return result;
}

void ConfigurationEditor::fail(const std::exception &error)
{
    status_ = QString::fromUtf8(error.what());
    emit changed();
}

void ConfigurationEditor::attach(const std::shared_ptr<sv::client::Client> &client)
{
    disconnected();
    client_ = client;
}

void ConfigurationEditor::disconnected()
{
    connected_ = snapshot_loaded_ = catalog_loaded_ = false;
    operations_supported_ = apply_supported_ = validated_ = false;
    read_id_ = catalog_id_ = validate_id_ = apply_id_ = status_id_ = 0;
    if (operation_ && operation_->state == Operation::State::Awaiting)
    {
        operation_->state = Operation::State::Unknown;
    }
    fusion_.clear();
    surface_.clear();
    telemetry_.clear();
    active_telemetry_.clear();
    catalog_.clear();
    base_revision_.clear();
    status_ = unresolved() ? "Результат операции неизвестен; подключитесь и проверьте статус"
                           : "Нет соединения с сервером";
    emit draftChanged();
    emit changed();
}

void ConfigurationEditor::connected()
{
    connected_ = true;
    refresh();
}

void ConfigurationEditor::refresh()
{
    try
    {
        if (busy())
        {
            throw std::runtime_error("configuration_request_pending");
        }
        const auto connection = client();
        validated_ = false;
        read_id_ = connection->read_configuration();
        catalog_id_ = connection->configuration_catalog();
        status_ = "Чтение active/persisted и каталога сервера";
    }
    catch (const std::exception &error)
    {
        fail(error);
    }
    emit changed();
}

void ConfigurationEditor::loadActive()
{
    if (!ready() || busy() || unresolved())
    {
        return;
    }
    fusion_ = active_fusion_;
    surface_ = map(snapshot_.active.at("surface"));
    telemetry_ = active_telemetry_;
    base_revision_ = QString::number(snapshot_.revision);
    validated_ = false;
    status_ = "Черновик active загружен; проверка и применение выполняются сервером";
    emit draftChanged();
    emit changed();
}

void ConfigurationEditor::consume(const sv::Message &message)
{
    if (message.type == 2)
    {
        if (const auto *capabilities = message.header.if_contains("capabilities"))
        {
            for (const auto &value : capabilities->as_array())
            {
                if (value.as_string() == "config_operations_v1")
                {
                    operations_supported_ = true;
                }
            }
        }
        return;
    }
    if (message.type != 21)
    {
        return;
    }
    const auto id = sv::parse_decimal_u64(std::string(message.header.at("command_id").as_string()));
    if (id == apply_id_ || id == status_id_)
    {
        receiveOperation(message);
        return;
    }
    if (id != read_id_ && id != catalog_id_ && id != validate_id_)
    {
        return;
    }
    const bool accepted = message.header.at("accepted").as_bool();
    if (!accepted)
    {
        if (id == read_id_)
        {
            read_id_ = 0;
            snapshot_loaded_ = false;
        }
        if (id == catalog_id_)
        {
            catalog_id_ = 0;
            catalog_loaded_ = false;
        }
        if (id == validate_id_)
        {
            validate_id_ = 0;
            validated_ = false;
        }
        status_ = "Отклонено сервером: " +
                  QString::fromStdString(std::string(message.header.at("reason").as_string()));
        emit changed();
        return;
    }
    if (id == read_id_)
    {
        snapshot_ = sv::client::parse_configuration_snapshot(message);
        active_fusion_ = map(message.header.at("fusion"));
        active_telemetry_ = {{"enabled", false}, {"period_ms", 100}, {"history_capacity", 128}};
        if (const auto *settings = snapshot_.active.if_contains("telemetry"))
        {
            const auto supplied = map(*settings);
            for (auto it = supplied.begin(); it != supplied.end(); ++it)
            {
                active_telemetry_[it.key()] = it.value();
            }
        }
        read_id_ = 0;
        snapshot_loaded_ = true;
        reconcile();
    }
    else if (id == catalog_id_)
    {
        const auto result = sv::client::parse_configuration_catalog(message);
        apply_supported_ = result.apply_supported;
        catalog_ = {{"fusion", map(result.fusion)}, {"surface", map(result.surface)}};
        if (!result.telemetry.empty())
        {
            catalog_["telemetry"] = map(result.telemetry);
        }
        catalog_id_ = 0;
        catalog_loaded_ = true;
    }
    else
    {
        const auto result = sv::client::parse_configuration_validation(message);
        if (QString::number(result.base_revision) != base_revision_)
        {
            throw std::runtime_error("configuration_validation_revision_mismatch");
        }
        validate_id_ = 0;
        validated_ = true;
        status_ = "Сервер проверил черновик; revision не зарезервирована";
    }
    if (ready() && base_revision_.isEmpty() && !busy() && !unresolved())
    {
        loadActive();
    }
    emit changed();
}

void ConfigurationEditor::commandFailed(const boost::json::object &header,
                                        const std::string &reason)
{
    const auto *field = header.if_contains("command_id");
    if (!field)
    {
        return;
    }
    const auto id = sv::parse_decimal_u64(std::string(field->as_string()));
    bool matched = false;
    for (auto *pending : {&read_id_, &catalog_id_, &validate_id_, &apply_id_, &status_id_})
    {
        if (*pending && *pending == id)
        {
            *pending = 0;
            matched = true;
        }
    }
    if (!matched)
    {
        return;
    }
    if (operation_ && operation_->state == Operation::State::Awaiting)
    {
        operation_->state = Operation::State::Unknown;
    }
    validated_ = false;
    status_ = "Запрос не подтверждён: " + QString::fromStdString(reason) +
              "; для операции проверьте серверный статус";
    emit changed();
}
