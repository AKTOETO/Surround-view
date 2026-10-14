#include "configuration_editor.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <stdexcept>

QString ConfigurationEditor::operationId() const
{
    return operation_ ? QString::fromStdString(operation_->id) : QString{};
}

QString ConfigurationEditor::operationState() const
{
    if (!operation_)
    {
        return "Нет операции";
    }
    switch (operation_->state)
    {
    case Operation::State::Awaiting:
        return "Ожидание ответа";
    case Operation::State::Unknown:
        return "Результат неизвестен";
    case Operation::State::NotFound:
        return "Не найдена в журнале";
    case Operation::State::Committed:
        return operation_->persist ? "Применена и сохранена" : "Применена временно";
    case Operation::State::Rejected:
        return "Отклонена";
    case Operation::State::InstanceChanged:
        return "Сервер перезапущен или выбран другой сервер";
    }
    return {};
}

QVariantList ConfigurationEditor::comparison() const
{
    QVariantList rows;
    if (!operation_ || !snapshot_loaded_)
    {
        return rows;
    }
    auto display = [](const boost::json::value *value)
    { return value ? QString::fromStdString(boost::json::serialize(*value)) : QString("—"); };
    for (const auto &group : operation_->changes)
    {
        const auto *active = snapshot_.active.if_contains(group.key());
        const auto *persisted = snapshot_.persisted.if_contains(group.key());
        for (const auto &field : group.value().as_object())
        {
            rows.append(QVariantMap{
                {"field",
                 QString::fromStdString(std::string(group.key()) + "." + std::string(field.key()))},
                {"requested", display(&field.value())},
                {"active", display(active && active->is_object()
                                       ? active->as_object().if_contains(field.key())
                                       : nullptr)},
                {"persisted", display(persisted && persisted->is_object()
                                          ? persisted->as_object().if_contains(field.key())
                                          : nullptr)}});
        }
    }
    return rows;
}

bool ConfigurationEditor::unresolved() const
{
    return operation_ && operation_->state != Operation::State::Committed &&
           operation_->state != Operation::State::Rejected;
}

bool ConfigurationEditor::canApply() const
{
    return ready() && !busy() && !unresolved() && apply_supported_ && operations_supported_ &&
           !snapshot_.server_instance_id.empty() && !base_revision_.isEmpty();
}

bool ConfigurationEditor::serverChanged() const
{
    return operation_ && operation_->state == Operation::State::InstanceChanged;
}

bool ConfigurationEditor::canRetry() const
{
    return ready() && operations_supported_ && !busy() && operation_ &&
           operation_->instance == snapshot_.server_instance_id &&
           (operation_->state == Operation::State::Unknown ||
            operation_->state == Operation::State::NotFound);
}

void ConfigurationEditor::sendOperation()
{
    const auto &op = *operation_;
    apply_id_ = client()->apply_configuration_once(op.instance, op.id, op.base_revision, op.changes,
                                                   op.persist);
    operation_->state = Operation::State::Awaiting;
    status_ = op.persist ? "Применение и сохранение всего active snapshot сервером"
                         : "Временное применение сервером";
}

void ConfigurationEditor::apply(bool persist)
{
    try
    {
        if (!canApply() || (persist && !canPersist()))
        {
            throw std::runtime_error("configuration_apply_unavailable");
        }
        Operation op{snapshot_.server_instance_id,
                     QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                     sv::parse_decimal_u64(base_revision_.toStdString()), changes(), persist};
        operation_ = std::move(op); // Retain identity/intent BEFORE submitting to the library.
        sendOperation();
    }
    catch (const std::exception &error)
    {
        if (operation_ && operation_->state == Operation::State::Awaiting)
        {
            operation_->state = Operation::State::Unknown;
        }
        fail(error);
    }
    emit changed();
}

void ConfigurationEditor::reconcile()
{
    if (!unresolved())
    {
        return;
    }
    if (operation_->instance != snapshot_.server_instance_id)
    {
        operation_->state = Operation::State::InstanceChanged;
        status_ = "Экземпляр сервера изменился: старую операцию не повторять. Сверьте "
                  "active/persisted и подтвердите сверку";
    }
    else if (!status_id_)
    {
        status_id_ = client()->configuration_operation_status(operation_->instance, operation_->id);
        status_ = "Проверка сохранённого сервером результата операции";
    }
}

void ConfigurationEditor::checkOperation()
{
    try
    {
        if (!ready() || busy() || !operation_)
        {
            throw std::runtime_error("configuration_status_unavailable");
        }
        if (operation_->instance != snapshot_.server_instance_id)
        {
            throw std::runtime_error("configuration_server_instance_changed_no_retry");
        }
        status_id_ = client()->configuration_operation_status(operation_->instance, operation_->id);
        status_ = "Запрос статуса операции";
    }
    catch (const std::exception &error)
    {
        fail(error);
    }
    emit changed();
}

void ConfigurationEditor::retryOperation()
{
    try
    {
        if (!canRetry())
        {
            throw std::runtime_error("configuration_retry_unavailable");
        }
        sendOperation(); // Exact retained intent, never current edited fields or new instance.
    }
    catch (const std::exception &error)
    {
        fail(error);
    }
    emit changed();
}

void ConfigurationEditor::acknowledgeUnknown()
{
    // Explicit human reconciliation after restart; never unlock an unknown same-process outcome.
    if (!ready() || busy() || !operation_ || operation_->state != Operation::State::InstanceChanged)
    {
        return;
    }
    operation_.reset();
    loadActive();
}

void ConfigurationEditor::receiveOperation(const sv::Message &message)
{
    const auto id = sv::parse_decimal_u64(std::string(message.header.at("command_id").as_string()));
    const bool status_query = id == status_id_;
    if (!message.header.contains("configuration_operation"))
    {
        if (status_query)
        {
            status_id_ = 0;
        }
        else
        {
            apply_id_ = 0;
        }
        if (operation_)
        {
            operation_->state = Operation::State::Unknown;
        }
        status_ = "Результат не установлен: " +
                  QString::fromStdString(std::string(message.header.at("reason").as_string()));
        emit changed();
        return;
    }
    const auto result = sv::client::parse_configuration_operation(message);
    if (!operation_ || result.server_instance_id != operation_->instance ||
        result.operation_id != operation_->id ||
        (result.state != "not_found" && result.base_revision != operation_->base_revision) ||
        (result.state == "committed" && result.persisted != operation_->persist))
    {
        throw std::runtime_error("configuration_operation_identity_mismatch");
    }
    if (status_query)
    {
        status_id_ = 0;
    }
    else
    {
        apply_id_ = 0;
    }
    if (result.state == "committed" || result.state == "rejected")
    {
        const bool completing = unresolved();
        operation_->state =
            result.state == "committed" ? Operation::State::Committed : Operation::State::Rejected;
        status_ = result.state == "committed"
                      ? (result.persisted ? "Сервер применил и сохранил весь active snapshot"
                                          : "Сервер применил временные настройки")
                      : "Отклонено сервером: " + QString::fromStdString(result.reason);
        if (result.state == "committed" && completing)
        {
            base_revision_.clear();
            emit draftChanged();
        }
        // Read authoritative current config, which may be newer than the retained result.
        snapshot_loaded_ = false;
        read_id_ = client()->read_configuration();
        validated_ = false;
    }
    else
    {
        operation_->state =
            result.state == "not_found" ? Operation::State::NotFound : Operation::State::Unknown;
        status_ = result.state == "not_found"
                      ? "Операция не найдена; можно явно повторить тот же ID/intent"
                      : "Операция не завершена; повторно проверьте статус";
    }
    emit changed();
}
