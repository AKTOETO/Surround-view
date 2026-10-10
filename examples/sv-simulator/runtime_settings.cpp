#include "runtime_settings.hpp"
#include <QJsonDocument>
#include <limits>
#include <stdexcept>

namespace
{
QString pretty(const boost::json::value &value)
{
    const auto json = boost::json::serialize(value);
    return QString::fromUtf8(
        QJsonDocument::fromJson(QByteArray::fromStdString(json)).toJson(QJsonDocument::Indented));
}
} // namespace

void RuntimeSettings::attach(const std::shared_ptr<sv::client::Client> &client)
{
    client_ = client;
    revision_.clear();
    fusion_.clear();
    surface_.clear();
    catalog_.clear();
    catalogs_.clear();
    pending_id_ = 0;
    status_ = "Ожидание состояния сервера";
    emit changed();
}

void RuntimeSettings::refresh()
{
    try
    {
        const auto client = client_.lock();
        if (!client)
        {
            throw std::runtime_error("not_connected");
        }
        client->state();
        client->fusion_catalog();
        client->surface_catalog();
    }
    catch (const std::exception &error)
    {
        status_ = QString::fromUtf8(error.what());
        emit changed();
    }
}

void RuntimeSettings::consume(const boost::json::object &header)
{
    // ACKs, including rejections, carry the actual server snapshot.
    if (header.contains("config_revision") && header.contains("fusion") &&
        header.contains("surface"))
    {
        if (revision_.isEmpty())
        {
            status_ = "Снимок сервера загружен; изменения временные";
        }
        revision_ = QString::fromStdString(std::string(header.at("config_revision").as_string()));
        fusion_ = pretty(header.at("fusion"));
        surface_ = pretty(header.at("surface"));
    }
    for (const auto *key : {"fusion_catalog", "surface_catalog"})
    {
        if (const auto *value = header.if_contains(key))
        {
            catalogs_[key] = *value;
        }
    }
    catalog_ = pretty(catalogs_);
    if (pending_id_ && header.contains("command_id") &&
        sv::parse_decimal_u64(std::string(header.at("command_id").as_string())) == pending_id_)
    {
        pending_id_ = 0;
        status_ = header.at("accepted").as_bool()
                      ? "Применено сервером; изменение временное"
                      : "Отклонено сервером: " +
                            QString::fromStdString(std::string(header.at("reason").as_string()));
    }
    emit changed();
}

void RuntimeSettings::applyFusion(const QString &json, const QString &baseRevision)
{
    apply(json, baseRevision, true);
}

void RuntimeSettings::commandFailed(const boost::json::object &header, const std::string &reason)
{
    const auto *id = header.if_contains("command_id");
    if (pending_id_ && id && sv::parse_decimal_u64(std::string(id->as_string())) == pending_id_)
    {
        pending_id_ = 0;
        // Transport failure is not proof that the server did not apply a command.
        status_ = "Запрос не подтверждён: " + QString::fromStdString(reason);
        emit changed();
    }
}

void RuntimeSettings::applySurface(const QString &json, const QString &baseRevision)
{
    apply(json, baseRevision, false);
}

void RuntimeSettings::apply(const QString &json, const QString &baseRevision, bool fusion)
{
    try
    {
        const auto client = client_.lock();
        if (!client || !ready())
        {
            throw std::runtime_error("state_not_loaded");
        }
        if (pending())
        {
            throw std::runtime_error("update_pending");
        }
        const auto revision = sv::parse_decimal_u64(baseRevision.toStdString());
        const auto object = boost::json::parse(json.toStdString()).as_object();
        if (fusion)
        {
            // No defaults or silent truncation: send exactly the six displayed fields.
            if (object.size() != 6)
            {
                throw std::runtime_error("fusion_requires_exactly_six_fields");
            }
            sv::client::FusionSettings settings;
            settings.mode = std::string(object.at("mode").as_string());
            settings.diagnostic = std::string(object.at("diagnostic").as_string());
            settings.edge_width_px = boost::json::value_to<double>(object.at("edge_width_px"));
            settings.angle_power = boost::json::value_to<double>(object.at("angle_power"));
            settings.smoothness_weight =
                boost::json::value_to<double>(object.at("smoothness_weight"));
            const auto &levels = object.at("pyramid_levels");
            if ((!levels.is_int64() && !levels.is_uint64()) ||
                boost::json::value_to<double>(levels) < 0 ||
                boost::json::value_to<double>(levels) > std::numeric_limits<unsigned>::max())
            {
                throw std::runtime_error("pyramid_levels_must_be_unsigned_integer");
            }
            settings.pyramid_levels = boost::json::value_to<unsigned>(levels);
            pending_id_ = client->configure_fusion(revision, settings);
        }
        else
        {
            pending_id_ = client->configure_surface(revision, object);
        }
        status_ = "Ожидание подтверждения сервера";
    }
    catch (const std::exception &error)
    {
        status_ = QString::fromUtf8(error.what());
    }
    emit changed();
}
