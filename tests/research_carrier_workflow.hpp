#pragma once
#include "connection.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <stdexcept>

namespace carrier_workflow
{
inline QVariantMap map(const boost::json::value &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(boost::json::serialize(value)))
        .object()
        .toVariantMap();
}

// Fixture dimensions are test inputs, never GUI defaults. Every snapshot is read back
// from the real server after it accepts the requested carrier.
inline void capture(QObject *panel, const sv::client::Options &options)
{
    sv::research::Connection connection(options);
    auto accepted = [&](const std::string &operation, boost::json::object parameters = {})
    {
        auto reply = connection.request(operation, std::move(parameters));
        if (!reply.at("accepted").as_bool())
        {
            throw std::runtime_error(boost::json::serialize(reply));
        }
        return reply;
    };
    const auto initial = accepted("state");
    auto revision = initial.at("config_revision");
    const auto original = initial.at("surface");
    const auto catalog = accepted("config_catalog").at("configuration_catalog").as_object();
    panel->setProperty("surfaceCatalog", map(catalog.at("surface")));
    const boost::json::array profiles{
        boost::json::object{{"type", "rectangular_bowl_v1"},
                            {"flat_half_length_m", 2.6},
                            {"flat_half_width_m", 1.2},
                            {"outer_half_length_m", 14.},
                            {"outer_half_width_m", 14.},
                            {"corner_height_m", 0.},
                            {"uniform_cells", boost::json::array{32, 45}}},
        boost::json::object{{"type", "rectangular_bowl_v1"},
                            {"flat_half_length_m", 2.6},
                            {"flat_half_width_m", 1.2},
                            {"outer_half_length_m", 14.},
                            {"outer_half_width_m", 14.},
                            {"corner_height_m", 1.5},
                            {"uniform_cells", boost::json::array{32, 45}}},
        boost::json::object{{"type", "dome_floor_v1"},
                            {"dome_radius_m", 14.},
                            {"dome_latitude_cells", 14},
                            {"dome_longitude_cells", 64},
                            {"floor_radial_cells", 12}},
        boost::json::object{{"type", "cylinder_floor_v1"},
                            {"radius_m", 14.},
                            {"height_m", 14.},
                            {"vertical_cells", 10},
                            {"angular_cells", 64},
                            {"floor_radial_cells", 8}},
        boost::json::object{{"type", "cube_floor_v1"},
                            {"half_extent_m", 14.},
                            {"height_m", 14.},
                            {"face_cells", 16}}};
    const auto apply = [&](const boost::json::value &surface)
    {
        revision = accepted("configure_surface",
                            {{"base_config_revision", revision}, {"surface", surface}})
                       .at("config_revision");
    };
    try
    {
        for (const auto &profile : profiles)
        {
            apply(profile);
            const auto read = accepted("config_read").at("configuration").as_object();
            const auto active = read.at("active").as_object().at("surface");
            if (active != profile)
            {
                throw std::runtime_error("active carrier differs from request");
            }
            panel->setProperty("activeSurface", map(active));
            panel->setProperty("canCaptureSurface", true);
            if (!QMetaObject::invokeMethod(panel->findChild<QObject *>("researchCaptureCarrier"),
                                           "clicked"))
            {
                throw std::runtime_error("cannot capture carrier through UI");
            }
        }
    }
    catch (...)
    {
        apply(original);
        throw;
    }
    apply(original);
    panel->findChild<QObject *>("researchUseCarriers")->setProperty("checked", true);
}
} // namespace carrier_workflow
