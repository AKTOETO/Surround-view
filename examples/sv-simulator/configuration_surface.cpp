#include "configuration_editor.hpp"
#include <algorithm>

namespace
{
bool replacePath(QVariant &node, const QStringList &path, int depth, const QVariant &value)
{
    if (node.userType() == QMetaType::QVariantMap)
    {
        auto object = node.toMap();
        const auto &key = path.at(depth);
        if (depth + 1 == path.size())
        {
            object[key] = value;
        }
        else
        {
            if (!object.contains(key))
            {
                return false;
            }
            auto child = object.value(key);
            if (!replacePath(child, path, depth + 1, value))
            {
                return false;
            }
            object[key] = child;
        }
        node = object;
        return true;
    }
    if (node.userType() == QMetaType::QVariantList)
    {
        auto array = node.toList();
        bool ok = false;
        const int index = path.at(depth).toInt(&ok);
        if (!ok || index < 0 || index >= array.size())
        {
            return false;
        }
        if (depth + 1 == path.size())
        {
            array[index] = value;
        }
        else
        {
            auto child = array[index];
            if (!replacePath(child, path, depth + 1, value))
            {
                return false;
            }
            array[index] = child;
        }
        node = array;
        return true;
    }
    return false;
}
} // namespace

bool ConfigurationEditor::replaceDraftPath(QVariantMap &draft, const QString &field,
                                           const QVariant &value)
{
    const auto path = field.split('.');
    if (path.isEmpty() || path.contains(QString{}))
    {
        return false;
    }
    QVariant root = draft;
    if (!replacePath(root, path, 0, value))
    {
        return false;
    }
    draft = root.toMap();
    return true;
}

void ConfigurationEditor::setOptionalEnabled(const QString &field, bool enabled)
{
    if (!ready() || busy() || unresolved() || base_revision_.isEmpty() ||
        surface_.value("type") != "rectangular_bowl_v1")
    {
        return;
    }
    const auto catalog = catalog_.value("surface").toMap();
    const QString descriptor = field == "reference_height_m"    ? "rectangular_reference_height"
                               : field == "local_support_patch" ? "rectangular_local_support"
                               : field == "source_occlusion_box" || field == "uncertainty"
                                   ? "rectangular_source_occlusion"
                                   : QString{};
    if (descriptor.isEmpty() || !catalog.contains(descriptor) ||
        (field == "uncertainty" && !catalog.value(descriptor).toMap().contains("uncertainty")))
    {
        status_ = "Сервер не объявил поддержку этой optional настройки";
        emit changed();
        return;
    }
    if (field == "uncertainty")
    {
        if (!surface_.contains("source_occlusion_box"))
        {
            return;
        }
        auto box = surface_.value("source_occlusion_box").toMap();
        if (enabled && !box.contains(field))
        {
            box[field] = QVariantMap{{"radius_m", 0.0}, {"ambiguous_policy", "accept"}};
        }
        else if (!enabled)
        {
            box.remove(field);
        }
        surface_["source_occlusion_box"] = box;
    }
    else if (!enabled)
    {
        surface_.remove(field);
    }
    else if (!surface_.contains(field))
    {
        if (field == "reference_height_m")
        {
            surface_[field] = catalog.value(descriptor).toMap().value("default", 0.0);
        }
        else if (field == "local_support_patch")
        {
            const double minimum = catalog.value(descriptor)
                                       .toMap()
                                       .value("minimum_extent_and_transition_m", .01)
                                       .toDouble();
            surface_[field] = QVariantMap{
                {"center_x_m", 0.0},
                {"center_y_m", 0.0},
                {"half_length_m",
                 std::max(minimum,
                          std::min(.5, surface_.value("outer_half_length_m").toDouble() / 4))},
                {"half_width_m",
                 std::max(minimum,
                          std::min(.5, surface_.value("outer_half_width_m").toDouble() / 4))},
                {"height_m", 0.0},
                {"transition_m", minimum}};
        }
        else
        {
            surface_[field] = QVariantMap{{"min_m", QVariantList{3.0, -.5, 0.0}},
                                          {"max_m", QVariantList{4.0, .5, 1.0}},
                                          {"tolerance_m", 0.0}};
        }
    }
    validated_ = false;
    status_ = "Optional настройка изменена в черновике; проверьте его на сервере";
    emit draftChanged();
    emit changed();
}

void ConfigurationEditor::normalizeOcclusionMode()
{
    if (!surface_.contains("source_occlusion_box"))
    {
        return;
    }
    auto box = surface_.value("source_occlusion_box").toMap();
    const auto mode = box.value("mode", "center").toString();
    if (mode != "bilinear_box_surface_v1")
    {
        box.remove("support_tolerance_m");
        box.remove("require_facing_surface");
    }
    if (mode == "center")
    {
        box.remove("guard_texels");
    }
    surface_["source_occlusion_box"] = box;
}
