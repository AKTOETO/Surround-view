#include "configuration_editor.hpp"
#include "sv/surface_runtime.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

extern sv::client::Endpoint simulator_runtime_endpoint;

struct ConfigurationEditorTestAccess
{
    static void prime(ConfigurationEditor &editor,
                      const std::shared_ptr<sv::client::Client> &client)
    {
        editor.attach(client);
        editor.connected_ = editor.operations_supported_ = editor.apply_supported_ = true;
        editor.read_id_ = 100;
        editor.catalog_loaded_ = true;
        editor.catalog_["surface"] =
            QJsonDocument::fromJson(
                QByteArray::fromStdString(boost::json::serialize(sv::surface_catalog())))
                .object()
                .toVariantMap();
    }

    static void removeSurfaceCatalog(ConfigurationEditor &editor)
    {
        editor.catalog_.remove("surface");
    }

    static uint64_t applyId(const ConfigurationEditor &editor)
    {
        return editor.apply_id_;
    }

    static uint64_t statusId(const ConfigurationEditor &editor)
    {
        return editor.status_id_;
    }

    static uint64_t readId(const ConfigurationEditor &editor)
    {
        return editor.read_id_;
    }

    static boost::json::object intent(const ConfigurationEditor &editor)
    {
        return editor.operation_->changes;
    }

    static uint64_t base(const ConfigurationEditor &editor)
    {
        return editor.operation_->base_revision;
    }

    static void restoredSnapshot(ConfigurationEditor &editor, const std::string &instance)
    {
        editor.connected_ = editor.snapshot_loaded_ = editor.catalog_loaded_ = true;
        editor.snapshot_.server_instance_id = instance;
        editor.reconcile();
    }
};

namespace
{
constexpr char instance[] = "0123456789abcdef0123456789abcdef";

sv::Message snapshot(uint64_t id, const std::string &server_instance = instance,
                     const std::string &revision = "0")
{
    boost::json::object fusion{{"mode", "edge_feather"},    {"diagnostic", "color"},
                               {"edge_width_px", 24},       {"angle_power", 2},
                               {"smoothness_weight", .1},   {"pyramid_levels", 4},
                               {"pyramid_boundary", "zero"}};
    boost::json::object active{
        {"fusion", fusion},
        {"surface",
         boost::json::object{{"type", "rectangular_bowl_v1"},
                             {"uniform_cells", boost::json::array{32, 32}},
                             {"flat_half_length_m", 2.6},
                             {"flat_half_width_m", 1.2},
                             {"outer_half_length_m", 6.0},
                             {"outer_half_width_m", 4.5},
                             {"corner_height_m", 1.5},
                             {"local_support_patch", boost::json::object{{"height_m", .1}}}}}};
    return {21,
            {{"command_id", std::to_string(id)},
             {"accepted", true},
             {"config_revision", revision},
             {"fusion", fusion},
             {"configuration", boost::json::object{{"schema_version", 1},
                                                   {"scope", "config_store"},
                                                   {"revision", revision},
                                                   {"server_instance_id", server_instance},
                                                   {"active", active},
                                                   {"persisted", active},
                                                   {"has_runtime_changes", false},
                                                   {"persistence_configured", true}}}},
            {}};
}

class EditorUnit : public testing::Test
{
  protected:

    QTemporaryDir directory;
    ConfigurationEditor editor;
    std::shared_ptr<sv::client::Client> client;

    void SetUp() override
    {
        sv::client::Options options;
        options.endpoint.directory = directory.path().toStdString();
        options.max_retries = 0;
        client = std::make_shared<sv::client::Client>(options, [](auto) {});
        ConfigurationEditorTestAccess::prime(editor, client);
        editor.consume(snapshot(100));
    }

    void TearDown() override
    {
        client->stop();
    }
};

bool waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return done();
}
} // namespace

TEST_F(EditorUnit, UncertainOutcomeLocksDraftAndRetainsExactRetryIntent)
{
    ASSERT_TRUE(editor.canApply());
    editor.setField("fusion", "mode", "angular_feather");
    editor.apply(true);
    ASSERT_TRUE(editor.unresolved());
    const auto id = editor.operationId();
    const auto intent = ConfigurationEditorTestAccess::intent(editor);
    EXPECT_TRUE(intent.contains("fusion"));
    EXPECT_FALSE(intent.contains("surface")); // Fusion-only edit does not rebuild mesh.
    editor.commandFailed(
        {{"command_id", std::to_string(ConfigurationEditorTestAccess::applyId(editor))}},
        "lost_ack");
    EXPECT_FALSE(editor.busy());
    EXPECT_FALSE(editor.canApply());
    editor.setField("fusion", "mode", "hard_best_angle");
    EXPECT_EQ(editor.fusion().value("mode").toString(), "angular_feather");
    ASSERT_TRUE(editor.canRetry());
    editor.retryOperation();
    EXPECT_EQ(editor.operationId(), id);
    EXPECT_EQ(ConfigurationEditorTestAccess::intent(editor), intent);
    EXPECT_EQ(ConfigurationEditorTestAccess::base(editor), 0U);
}

TEST_F(EditorUnit, RestartNeverRebindsOldRequestAndNeedsExplicitReconciliation)
{
    editor.apply(false);
    const auto id = editor.operationId();
    editor.disconnected();
    EXPECT_TRUE(editor.unresolved());
    EXPECT_FALSE(editor.ready());
    ConfigurationEditorTestAccess::restoredSnapshot(editor, "another-server-instance");
    EXPECT_FALSE(editor.canRetry());
    EXPECT_FALSE(editor.canApply());
    EXPECT_EQ(ConfigurationEditorTestAccess::statusId(editor), 0U);
    editor.retryOperation();
    EXPECT_EQ(editor.operationId(), id);
    editor.acknowledgeUnknown();
    EXPECT_FALSE(editor.unresolved());
    EXPECT_TRUE(editor.operationId().isEmpty());
}

TEST_F(EditorUnit, SameProcessReconnectQueriesStatusWithoutAutomaticReplay)
{
    editor.apply(false);
    const auto id = editor.operationId();
    editor.disconnected();
    ConfigurationEditorTestAccess::restoredSnapshot(editor, instance);
    EXPECT_NE(ConfigurationEditorTestAccess::statusId(editor), 0U);
    EXPECT_EQ(ConfigurationEditorTestAccess::applyId(editor), 0U);
    EXPECT_EQ(editor.operationId(), id);
}

TEST_F(EditorUnit, SurfaceEditPreservesOptionalFieldsAndSwitchingTypeDiscardsThemExplicitly)
{
    editor.setField("surface", "uniform_cells.0", 40);
    EXPECT_EQ(editor.surface().value("uniform_cells").toList().at(0).toInt(), 40);
    EXPECT_TRUE(editor.surface().contains("local_support_patch"));
    editor.setSurfaceType("dome_floor_v1");
    EXPECT_FALSE(editor.surface().contains("local_support_patch"));
    EXPECT_FALSE(editor.surface().contains("uniform_cells"));
    EXPECT_EQ(editor.surface().value("dome_radius_m").toDouble(), 15.0);
}

TEST_F(EditorUnit, UnrelatedAcknowledgementCannotOverwriteDraftOrFinishPendingOperation)
{
    editor.setField("fusion", "mode", "angular_feather");
    editor.apply(false);
    editor.consume(snapshot(999, instance, "9"));
    EXPECT_TRUE(editor.busy());
    EXPECT_EQ(editor.baseRevision(), "0");
    EXPECT_EQ(editor.fusion().value("mode").toString(), "angular_feather");
}

TEST_F(EditorUnit, OptionalTogglesRemoveOnlySelectedFieldsAndPreserveOtherGroups)
{
    const auto original_fusion = editor.fusion();
    const auto cells = editor.surface().value("uniform_cells");
    editor.setOptionalEnabled("reference_height_m", true);
    EXPECT_TRUE(editor.surface().contains("reference_height_m"));
    editor.setField("surface", "reference_height_m", .25);
    editor.setOptionalEnabled("local_support_patch", false);
    editor.setOptionalEnabled("local_support_patch", true);
    editor.setField("surface", "local_support_patch.center_x_m", -1.0);
    editor.setField("surface", "local_support_patch.height_m", .5);
    editor.setOptionalEnabled("source_occlusion_box", true);
    editor.setOptionalEnabled("uncertainty", true);
    editor.setField("surface", "source_occlusion_box.min_m.1", -.75);
    editor.setField("surface", "source_occlusion_box.uncertainty.radius_m", .012345);
    editor.setField("surface", "source_occlusion_box.uncertainty.ambiguous_policy", "reject");
    const auto box = editor.surface().value("source_occlusion_box").toMap();
    EXPECT_DOUBLE_EQ(box.value("min_m").toList().at(1).toDouble(), -.75);
    EXPECT_DOUBLE_EQ(box.value("uncertainty").toMap().value("radius_m").toDouble(), .012345);
    editor.setOptionalEnabled("reference_height_m", false);
    EXPECT_FALSE(editor.surface().contains("reference_height_m"));
    EXPECT_EQ(editor.surface().value("source_occlusion_box").toMap(), box);
    editor.setOptionalEnabled("uncertainty", false);
    EXPECT_FALSE(editor.surface().value("source_occlusion_box").toMap().contains("uncertainty"));
    EXPECT_TRUE(editor.surface().contains("local_support_patch"));
    editor.setOptionalEnabled("source_occlusion_box", false);
    EXPECT_FALSE(editor.surface().contains("source_occlusion_box"));
    EXPECT_EQ(editor.surface().value("uniform_cells"), cells);
    EXPECT_EQ(editor.fusion(), original_fusion);
    editor.apply(false);
    const auto intent = ConfigurationEditorTestAccess::intent(editor);
    EXPECT_EQ(intent.size(), 1U);
    EXPECT_TRUE(intent.contains("surface"));
    EXPECT_FALSE(intent.at("surface").as_object().contains("source_occlusion_box"));
}

TEST_F(EditorUnit, ModeTransitionRemovesIllegalFieldsButRetainsUncertaintyAndBounds)
{
    editor.setOptionalEnabled("source_occlusion_box", true);
    editor.setOptionalEnabled("uncertainty", true);
    editor.setField("surface", "source_occlusion_box.mode", "bilinear_box_surface_v1");
    editor.setField("surface", "source_occlusion_box.guard_texels", 1);
    editor.setField("surface", "source_occlusion_box.require_facing_surface", true);
    editor.setField("surface", "source_occlusion_box.support_tolerance_m", .000012);
    const auto initial = editor.surface().value("source_occlusion_box").toMap();
    editor.setField("surface", "source_occlusion_box.mode", "bilinear_radial_v1");
    auto box = editor.surface().value("source_occlusion_box").toMap();
    EXPECT_FALSE(box.contains("support_tolerance_m"));
    EXPECT_FALSE(box.contains("require_facing_surface"));
    EXPECT_EQ(box.value("guard_texels").toInt(), 1);
    EXPECT_EQ(box.value("uncertainty"), initial.value("uncertainty"));
    editor.setField("surface", "source_occlusion_box.mode", "center");
    box = editor.surface().value("source_occlusion_box").toMap();
    EXPECT_FALSE(box.contains("guard_texels"));
    EXPECT_EQ(box.value("min_m"), initial.value("min_m"));
    EXPECT_EQ(box.value("max_m"), initial.value("max_m"));
}

TEST_F(EditorUnit, InvalidNestedPathsAndUncertainOperationNeverModifyDraft)
{
    editor.setOptionalEnabled("source_occlusion_box", true);
    const auto before = editor.surface();
    editor.setField("surface", "source_occlusion_box.min_m.3", 4);
    editor.setField("surface", "source_occlusion_box.min_m.-1", 4);
    editor.setField("surface", "source_occlusion_box.min_m.invalid", 4);
    editor.setField("surface", "source_occlusion_box..tolerance_m", .1);
    editor.setField("surface", "source_occlusion_box.uncertainty.radius_m", .01);
    EXPECT_EQ(editor.surface(), before);
    editor.apply(false);
    editor.commandFailed(
        {{"command_id", std::to_string(ConfigurationEditorTestAccess::applyId(editor))}},
        "lost_ack");
    editor.setOptionalEnabled("source_occlusion_box", false);
    editor.setField("surface", "source_occlusion_box.min_m.0", -9);
    EXPECT_EQ(editor.surface(), before);
}

TEST_F(EditorUnit, OptionalControlsRequireRectangularCarrierAndAdvertisedCatalog)
{
    editor.setSurfaceType("dome_floor_v1");
    const auto dome = editor.surface();
    editor.setOptionalEnabled("reference_height_m", true);
    editor.setOptionalEnabled("source_occlusion_box", true);
    EXPECT_EQ(editor.surface(), dome);
    editor.setSurfaceType("rectangular_bowl_v1");
    const auto rectangular = editor.surface();
    ConfigurationEditorTestAccess::removeSurfaceCatalog(editor);
    editor.setOptionalEnabled("reference_height_m", true);
    editor.setOptionalEnabled("source_occlusion_box", true);
    EXPECT_EQ(editor.surface(), rectangular);
    EXPECT_TRUE(editor.status().contains("не объявил"));
}

TEST(SimulatorRuntime, ConfigServiceFormsValidateApplyAndRestore)
{
    ConfigurationEditor editor;
    sv::client::Options options;
    options.endpoint = simulator_runtime_endpoint;
    std::shared_ptr<sv::client::Client> client;
    QString failure;
    client = std::make_shared<sv::client::Client>(
        options,
        [&](sv::client::Event event)
        {
            QMetaObject::invokeMethod(
                &editor,
                [&, event = std::move(event)]
                {
                    try
                    {
                        if (event.kind == sv::client::Event::Kind::State)
                        {
                            if (event.detail == "ready")
                            {
                                editor.connected();
                            }
                            else
                            {
                                editor.disconnected();
                            }
                        }
                        if (event.kind == sv::client::Event::Kind::Error && event.message)
                        {
                            editor.commandFailed(event.message->header, event.detail);
                        }
                        if (event.kind == sv::client::Event::Kind::Message)
                        {
                            editor.consume(*event.message);
                            if (event.message->type == 11)
                            {
                                client->release(event.message->header);
                            }
                        }
                    }
                    catch (const std::exception &error)
                    {
                        failure = error.what();
                    }
                },
                Qt::QueuedConnection);
        });
    editor.attach(client);
    ASSERT_TRUE(waitFor([&] { return editor.canApply() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    const auto original_fusion = editor.fusion();
    const auto original_surface = editor.surface();
    const auto base = editor.baseRevision();
    editor.setField("fusion", "mode", "angular_feather");
    editor.validate();
    ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
    EXPECT_TRUE(editor.validated()) << editor.status().toStdString();
    editor.setField("fusion", "angle_power", 3.0);
    EXPECT_FALSE(editor.validated());
    editor.apply(false);
    ASSERT_TRUE(waitFor([&] { return !editor.busy() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    EXPECT_FALSE(editor.unresolved());
    EXPECT_NE(editor.baseRevision(), base);
    EXPECT_EQ(editor.fusion().value("angle_power").toDouble(), 3.0);
    editor.setField("surface", "uniform_cells.0", 24);
    editor.apply(false);
    ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
    EXPECT_EQ(editor.surface().value("uniform_cells").toList().at(0).toInt(), 24);
    const auto valid_revision = editor.serverRevision();
    editor.setField("fusion", "mode", "unknown_algorithm");
    editor.validate();
    ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
    EXPECT_FALSE(editor.validated());
    EXPECT_EQ(editor.serverRevision(), valid_revision);
    EXPECT_TRUE(editor.status().contains("Отклонено сервером"));
    editor.loadActive();
    editor.setOptionalEnabled("reference_height_m", true);
    editor.setField("surface", "reference_height_m", .1);
    editor.setOptionalEnabled("local_support_patch", true);
    editor.setField("surface", "local_support_patch.height_m", .2);
    editor.setOptionalEnabled("source_occlusion_box", true);
    editor.setOptionalEnabled("uncertainty", true);
    editor.setField("surface", "source_occlusion_box.uncertainty.radius_m", .005);
    editor.setField("surface", "source_occlusion_box.uncertainty.ambiguous_policy", "reject");
    for (const auto &mode : {"bilinear_box_surface_v1", "bilinear_radial_v1", "center"})
    {
        editor.setField("surface", "source_occlusion_box.mode", mode);
        if (std::string(mode) == "bilinear_box_surface_v1")
        {
            editor.setField("surface", "source_occlusion_box.guard_texels", 1);
            editor.setField("surface", "source_occlusion_box.require_facing_surface", true);
            editor.setField("surface", "source_occlusion_box.support_tolerance_m", .000012);
        }
        editor.validate();
        ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
        ASSERT_TRUE(editor.validated()) << editor.status().toStdString();
        editor.apply(false);
        ASSERT_TRUE(waitFor([&] { return !editor.busy() || !failure.isEmpty(); }));
        ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
        ASSERT_FALSE(editor.unresolved());
        EXPECT_EQ(editor.surface().value("source_occlusion_box").toMap().value("mode").toString(),
                  mode);
    }
    // Cross-field failure must preserve the active revision and the editable rejected draft.
    const auto before_invalid = editor.serverRevision();
    editor.setField("surface", "source_occlusion_box.min_m.0", 10.0);
    editor.validate();
    ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
    EXPECT_FALSE(editor.validated());
    EXPECT_EQ(editor.serverRevision(), before_invalid);
    EXPECT_TRUE(editor.status().contains("ordered bounds")) << editor.status().toStdString();
    editor.loadActive();
    editor.setOptionalEnabled("reference_height_m", false);
    editor.setOptionalEnabled("local_support_patch", false);
    editor.setOptionalEnabled("source_occlusion_box", false);
    for (auto it = original_fusion.begin(); it != original_fusion.end(); ++it)
    {
        editor.setField("fusion", it.key(), it.value());
    }
    for (auto it = original_surface.begin(); it != original_surface.end(); ++it)
    {
        editor.setField("surface", it.key(), it.value());
    }
    editor.apply(false);
    ASSERT_TRUE(waitFor([&] { return !editor.busy(); }));
    EXPECT_EQ(editor.fusion(), original_fusion);
    EXPECT_EQ(editor.surface(), original_surface);
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    client->stop();
}

TEST(SimulatorConfigurationPersistent, SavesCompleteRuntimeSnapshotThroughClientLibrary)
{
    ConfigurationEditor editor;
    sv::client::Options options;
    options.endpoint = simulator_runtime_endpoint;
    QString failure;
    std::shared_ptr<sv::client::Client> client;
    client = std::make_shared<sv::client::Client>(
        options,
        [&](sv::client::Event event)
        {
            QMetaObject::invokeMethod(
                &editor,
                [&, event = std::move(event)]
                {
                    try
                    {
                        if (event.kind == sv::client::Event::Kind::State)
                        {
                            if (event.detail == "ready")
                            {
                                editor.connected();
                            }
                            else
                            {
                                editor.disconnected();
                            }
                        }
                        if (event.kind == sv::client::Event::Kind::Error && event.message)
                        {
                            editor.commandFailed(event.message->header, event.detail);
                        }
                        if (event.kind == sv::client::Event::Kind::Message)
                        {
                            editor.consume(*event.message);
                            if (event.message->type == 11)
                            {
                                client->release(event.message->header);
                            }
                        }
                    }
                    catch (const std::exception &error)
                    {
                        failure = error.what();
                    }
                },
                Qt::QueuedConnection);
        });
    editor.attach(client);
    ASSERT_TRUE(waitFor([&] { return editor.canApply() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    editor.setField("fusion", "mode", "angular_feather");
    editor.apply(false);
    ASSERT_TRUE(waitFor([&] { return !editor.busy() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    EXPECT_TRUE(editor.hasRuntimeChanges());
    EXPECT_TRUE(editor.canPersist());
    const auto runtime_id = editor.operationId();
    editor.setField("fusion", "angle_power", 3.0);
    editor.apply(true);
    ASSERT_TRUE(waitFor([&] { return !editor.busy() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    EXPECT_NE(editor.operationId(), runtime_id);
    EXPECT_FALSE(editor.unresolved());
    EXPECT_FALSE(editor.hasRuntimeChanges());
    EXPECT_TRUE(editor.operationState().contains("сохранена"));
    EXPECT_EQ(editor.fusion().value("mode").toString(), "angular_feather");
    EXPECT_EQ(editor.fusion().value("angle_power").toDouble(), 3.0);
    client->stop();
}
