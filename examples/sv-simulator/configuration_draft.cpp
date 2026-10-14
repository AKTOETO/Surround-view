#include "configuration_editor.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

boost::json::object ConfigurationEditor::changes() const
{
    if (base_revision_.isEmpty())
    {
        throw std::runtime_error("configuration_draft_not_loaded");
    }
    QVariantMap groups;
    if (fusion_ != active_fusion_)
    {
        groups["fusion"] = fusion_;
    }
    const auto active_surface =
        QJsonDocument::fromJson(
            QByteArray::fromStdString(boost::json::serialize(snapshot_.active.at("surface"))))
            .object()
            .toVariantMap();
    if (surface_ != active_surface)
    {
        groups["surface"] = surface_;
    }
    // A no-edit save still needs a supported replacement; fusion avoids unnecessary mesh work.
    if (groups.isEmpty())
    {
        groups["fusion"] = fusion_;
    }
    const auto json =
        QJsonDocument(QJsonObject::fromVariantMap(groups)).toJson(QJsonDocument::Compact);
    return boost::json::parse(std::string(json.constData(), size_t(json.size()))).as_object();
}

void ConfigurationEditor::setField(const QString &group, const QString &field,
                                   const QVariant &value)
{
    if (!ready() || busy() || unresolved() || base_revision_.isEmpty())
    {
        return;
    }
    auto *draft = group == "fusion" ? &fusion_ : group == "surface" ? &surface_ : nullptr;
    if (!draft)
    {
        return;
    }
    // Only field names from the forms are supplied here; final validation is server-side.
    if (field == "fallback_display.mode")
    {
        (*draft)["fallback_display"] = QVariantMap{{"mode", value}};
    }
    else if (field.startsWith("uniform_cells."))
    {
        auto cells = draft->value("uniform_cells").toList();
        bool ok = false;
        const int index = field.mid(14).toInt(&ok);
        if (!ok || index < 0 || index >= cells.size())
        {
            return;
        }
        cells[index] = value;
        (*draft)["uniform_cells"] = cells;
    }
    else
    {
        (*draft)[field] = value;
    }
    validated_ = false;
    emit draftChanged();
    emit changed();
}

void ConfigurationEditor::setSurfaceType(const QString &type)
{
    if (!ready() || busy() || unresolved())
    {
        return;
    }
    if (type == surface_.value("type").toString())
    {
        return;
    }
    // Initial drafts, not accepted configurations; view clearance still needs server validation.
    if (type == "rectangular_bowl_v1")
    {
        surface_ = {{"type", type},
                    {"flat_half_length_m", 2.6},
                    {"flat_half_width_m", 1.2},
                    {"outer_half_length_m", 6.0},
                    {"outer_half_width_m", 4.5},
                    {"corner_height_m", 1.5},
                    {"uniform_cells", QVariantList{32, 32}}};
    }
    else if (type == "dome_floor_v1")
    {
        surface_ = {{"type", type},
                    {"dome_radius_m", 15.0},
                    {"dome_latitude_cells", 32},
                    {"dome_longitude_cells", 64},
                    {"floor_radial_cells", 16}};
    }
    else if (type == "cylinder_floor_v1")
    {
        surface_ = {{"type", type},         {"radius_m", 15.0},    {"height_m", 15.0},
                    {"vertical_cells", 16}, {"angular_cells", 64}, {"floor_radial_cells", 16}};
    }
    else if (type == "cube_floor_v1")
    {
        surface_ = {
            {"type", type}, {"half_extent_m", 15.0}, {"height_m", 15.0}, {"face_cells", 16}};
    }
    else
    {
        return;
    }
    validated_ = false;
    status_ = "Новый черновик носителя; прежние optional support/occlusion поля сброшены";
    emit draftChanged();
    emit changed();
}

void ConfigurationEditor::validate()
{
    try
    {
        if (!ready() || busy() || unresolved())
        {
            throw std::runtime_error("configuration_not_editable");
        }
        validate_id_ = client()->validate_configuration(
            sv::parse_decimal_u64(base_revision_.toStdString()), changes());
        validated_ = false;
        status_ = "Проверка черновика сервером";
    }
    catch (const std::exception &error)
    {
        fail(error);
    }
    emit changed();
}
