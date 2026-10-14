#include "sv/renderer.hpp"
#include <gtest/gtest.h>

TEST(RenderSurfaceTransaction, AbandonedPreparationPreservesPixelsAndCommitPreservesUploads)
{
    auto config = sv::load_config(SV_TEST_CONFIG);
    config.width = 96;
    config.height = 64;
    sv::FrameSet frames;
    for (int camera = 0; camera < 4; ++camera)
    {
        auto image = std::make_shared<sv::Image>();
        image->width = config.cameras[camera].width;
        image->height = config.cameras[camera].height;
        image->channels = 3;
        image->pixels.resize(image->width * image->height * 3,
                             static_cast<unsigned char>(40 + camera * 50));
        sv::Frame frame;
        frame.camera_id = camera;
        frame.image = image;
        frames.frames[camera] = frame;
    }
    sv::Renderer renderer(config);
    const auto before = renderer.render(frames, config.view);
    const auto builds = renderer.mesh_builds(), uploads = renderer.uploads();
    const auto triangles = renderer.triangles();
    auto changed = config.surface;
    changed.type = "rectangular_bowl_v1";
    changed.H = 0;
    changed.nx = 12;
    changed.ny = 10;
    changed.A = changed.B = 14;
    {
        auto abandoned = renderer.prepare_surface(changed);
        EXPECT_EQ(renderer.mesh_builds(), builds);
        EXPECT_EQ(renderer.triangles(), triangles);
        EXPECT_EQ(renderer.render(frames, config.view).pixels, before.pixels);
    }
    EXPECT_EQ(renderer.render(frames, config.view).pixels, before.pixels);
    auto update = renderer.prepare_surface(changed);
    update->commit();
    EXPECT_EQ(renderer.mesh_builds(), builds + 1);
    EXPECT_EQ(renderer.triangles(), 336U); // 2*(12+2)*(10+2): inserted flat-boundary grid lines.
    update->commit();
    EXPECT_EQ(renderer.mesh_builds(), builds + 1); // Publication is one-shot.
    const auto after = renderer.render(frames, config.view);
    EXPECT_EQ(renderer.uploads(), uploads);
    EXPECT_NE(after.pixels, before.pixels);
    update.reset();
    EXPECT_EQ(renderer.render(frames, config.view).pixels, after.pixels);
}
