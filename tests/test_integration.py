import copy,json,os,select,socket,subprocess,sys,tempfile,time,unittest
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from simulator import generate,project
from ipc import Client,pack,receive
BUILD=Path(sys.argv[1]);CONFIG=Path(sys.argv[2])


class IntegrationTests(unittest.TestCase):
    def test_replay_controls_and_release(self):
        with tempfile.TemporaryDirectory(prefix='sv-integration-') as td:
            directory=Path(td);cfg=json.loads(CONFIG.read_text());manifest=generate(directory/'fixture',cfg,8)
            ipc=directory/'ipc';trace=directory/'trace.jsonl'
            server=subprocess.Popen([str(BUILD/'sv-server'),'--config',str(CONFIG),'--manifest',str(manifest),'--ipc-dir',str(ipc),'--trace',str(trace)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            client=None
            try:
                deadline=time.monotonic()+8
                while not (ipc/'data.sock').exists():
                    if server.poll() is not None:raise RuntimeError(server.communicate()[1])
                    if time.monotonic()>deadline:raise TimeoutError('server startup')
                    time.sleep(.02)
                client=Client(ipc)
                header,pixels=client.frame();self.assertEqual(header['health'],'READY');self.assertGreater(np.frombuffer(pixels,np.uint8).std(),10)
                ack=client.command('pause');self.assertTrue(ack['accepted'])
                # Drain the last pre-pause output and wait for the accepted revision.
                while True:
                    header,pixels=client.frame()
                    if int(header['state_revision'])>=int(ack['state_revision']):break
                uploads=header['upload_count'];before=pixels
                ack=client.command('preset',name='top')
                while True:
                    header,pixels=client.frame()
                    if int(header['state_revision'])>=int(ack['state_revision']):break
                self.assertNotEqual(before,pixels);self.assertEqual(uploads,header['upload_count'],'paused orbit must not upload inputs')
                rejected=client.command('preset',name='invalid');self.assertFalse(rejected['accepted'])
                accepted=client.command('orbit',azimuth_delta_rad=.2,elevation_delta_rad=0.);self.assertTrue(accepted['accepted'])
                client.frame()
                # Stop releasing frames; control must continue and the data session expires.
                client.command('zoom',distance_delta_m=.1);kind,unreleased,_=receive(client.data);self.assertEqual(kind,11)
                self.assertTrue(client.command('preset',name='rear')['accepted'])
                time.sleep(.35)
                with self.assertRaises(EOFError):receive(client.data)
                client.close();client=None;time.sleep(.05)
                client=Client(ipc);self.assertTrue(client.command('resume')['accepted']);self.assertEqual(client.frame()[0]['health'],'READY')
                # RELEASE has no binary body. A matching token cannot make a
                # malformed message free the slot and deliver another frame.
                client.command('preset',name='front')
                kind,held,_=receive(client.data);self.assertEqual(kind,11)
                client.data.sendall(pack(22,{k:held[k] for k in ('session_id','frame_id','buffer_token')},b'invalid-body'))
                with self.assertRaises(EOFError):receive(client.data)
                self.assertTrue(client.command('state')['accepted'],'bad data packet must not kill control')
                self.assertIsNone(server.poll(),'bad release must not terminate server')
                client.close();client=None
                # Malformed stream must be rejected without terminating the server.
                bad=socket.socket(socket.AF_UNIX);bad.settimeout(2);bad.connect(str(ipc/'control.sock'));bad.sendall(b'BAD!'+bytes(20));self.assertEqual(bad.recv(1),b'');bad.close();self.assertIsNone(server.poll())
            finally:
                if client:client.close()
                server.terminate()
                stdout,stderr=server.communicate(timeout=5)
                if server.returncode not in [0,-15]:raise RuntimeError(stderr)
            events=[json.loads(line) for line in trace.read_text().splitlines()]
            self.assertTrue(any(e['event']=='rendered' for e in events));self.assertEqual(events[-1]['event'],'shutdown')
            self.assertFalse((ipc/'data.sock').exists())


    def test_runtime_fusion_on_identical_paused_inputs(self):
        with tempfile.TemporaryDirectory(prefix='sv-fusion-runtime-') as td:
            directory = Path(td)
            cfg = json.loads(CONFIG.read_text())
            cfg['output'].update(width=160, height=96)
            config = directory/'server.json'
            config.write_text(json.dumps(cfg))
            original_bytes = config.read_bytes()
            manifest = generate(directory/'fixture', cfg, 8)
            ipc = directory/'ipc'
            server = subprocess.Popen([str(BUILD/'sv-server'), '--config', str(config),
                '--manifest', str(manifest), '--ipc-dir', str(ipc),
                '--trace', str(directory/'trace.jsonl')], stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True)
            client = None
            try:
                deadline = time.monotonic()+8
                while not (ipc/'data.sock').exists():
                    if server.poll() is not None:
                        raise RuntimeError(server.communicate()[1])
                    if time.monotonic()>deadline:
                        raise TimeoutError('startup')
                    time.sleep(.02)
                client = Client(ipc)
                client.frame()
                paused = client.command('pause')
                self.assertTrue(paused['accepted'])

                def after(ack):
                    for _ in range(8):
                        header, pixels = client.frame()
                        if int(header['state_revision']) >= int(ack['state_revision']):
                            return header, pixels
                    self.fail('no frame for applied revision')

                baseline, original_pixels = after(paused)
                state = client.command('state')
                original_fusion = state['fusion']
                revision = state['config_revision']
                catalog = client.command('fusion_catalog')
                self.assertTrue(catalog['accepted'])
                self.assertEqual(catalog['state_revision'], state['state_revision'])
                self.assertEqual(set(catalog['fusion_catalog']['modes']),
                    {'edge_feather', 'hard_best_angle', 'angular_feather',
                     'seam_distance_feather', 'graph_cut_seam', 'multi_band',
                     'graph_cut_multi_band'})
                for mode in catalog['fusion_catalog']['modes']:
                    settings = dict(mode=mode, diagnostic='weights',
                                    edge_width_px=12., angle_power=4.,
                                    pyramid_levels=4, smoothness_weight=.1, pyramid_boundary="zero")
                    ack = client.command('configure_fusion',
                        base_config_revision=revision, fusion=settings)
                    self.assertTrue(ack['accepted'], ack)
                    self.assertEqual(int(ack['config_revision']), int(revision)+1)
                    revision = ack['config_revision']
                    header, pixels = after(ack)
                    self.assertEqual(header['config_revision'], revision)
                    self.assertEqual(header['fusion'], settings)
                    self.assertEqual(header['frame_set_id'], baseline['frame_set_id'])
                    self.assertEqual(header['inputs'], baseline['inputs'])
                    self.assertEqual(header['upload_count'], baseline['upload_count'])
                    self.assertEqual(header['mesh_build_count'], baseline['mesh_build_count'])
                    self.assertNotEqual(pixels, original_pixels)
                before = client.command('state')
                cases = [dict(base_config_revision='0', fusion=original_fusion),
                         dict(base_config_revision=revision, fusion={'mode': 'missing'}),
                         dict(base_config_revision=revision,
                              fusion=dict(mode='edge_feather', angle_power=-1)),
                         dict(base_config_revision=revision,
                              fusion=dict(mode='edge_feather', unexpected=True))]
                for parameters in cases:
                    rejected = client.command('configure_fusion', **parameters)
                    self.assertFalse(rejected['accepted'], rejected)
                    self.assertEqual(rejected['config_revision'], revision)
                    self.assertEqual(rejected['state_revision'], before['state_revision'])
                    self.assertEqual(rejected['fusion'], before['fusion'])
                restored = client.command('configure_fusion',
                    base_config_revision=revision, fusion=original_fusion)
                self.assertTrue(restored['accepted'])
                header, pixels = after(restored)
                self.assertEqual(pixels, original_pixels)
                self.assertEqual(header['inputs'], baseline['inputs'])
                revision = restored['config_revision']
                original_surface = state['surface']
                flat = dict(original_surface, corner_height_m=0)
                surfaces = [flat,
                    dict(type='dome_floor_v1', dome_radius_m=14,
                         dome_latitude_cells=16, dome_longitude_cells=32, floor_radial_cells=8),
                    dict(type='cylinder_floor_v1', radius_m=14, height_m=14,
                         vertical_cells=8, angular_cells=32, floor_radial_cells=8),
                    dict(type='cube_floor_v1', half_extent_m=14, height_m=14, face_cells=8)]
                self.assertTrue(client.command('surface_catalog')['accepted'])
                builds = int(header['mesh_build_count'])
                for surface in surfaces:
                    ack = client.command('configure_surface', base_config_revision=revision,
                                         surface=surface)
                    self.assertTrue(ack['accepted'], ack)
                    revision = ack['config_revision']
                    header, pixels = after(ack)
                    builds += 1
                    self.assertEqual(int(header['mesh_build_count']), builds)
                    self.assertEqual(header['surface'], surface)
                    self.assertEqual(header['inputs'], baseline['inputs'])
                    self.assertEqual(header['upload_count'], baseline['upload_count'])
                    self.assertNotEqual(pixels, original_pixels)
                before = client.command('state')
                for rev, surface in [('0', original_surface), (revision, {'type': 'missing'}),
                                     (revision, dict(surfaces[1], dome_radius_m=3))]:
                    ack = client.command('configure_surface', base_config_revision=rev, surface=surface)
                    self.assertFalse(ack['accepted'], ack)
                    self.assertEqual(ack['surface'], before['surface'])
                    self.assertEqual(ack['state_revision'], before['state_revision'])
                    self.assertEqual(ack['config_revision'], revision)
                ack = client.command('configure_surface', base_config_revision=revision,
                                     surface=original_surface)
                self.assertTrue(ack['accepted'], ack)
                header, pixels = after(ack)
                self.assertEqual(pixels, original_pixels)
                self.assertEqual(config.read_bytes(), original_bytes)
            finally:
                if client:
                    client.close()
                server.terminate()
                _, stderr = server.communicate(timeout=5)
                if server.returncode not in [0, -15]:
                    raise RuntimeError(stderr)

    def test_calibration_provenance_rejection_and_status(self):
        with tempfile.TemporaryDirectory(prefix='sv-calibration-protocol-') as td:
            directory = Path(td)
            cfg = json.loads(CONFIG.read_text())
            manifest = generate(directory/'fixture',cfg,4)
            ipc = directory/'ipc'
            server = subprocess.Popen([str(BUILD/'sv-server'),'--config',str(CONFIG),
                '--manifest',str(manifest),'--ipc-dir',str(ipc),'--trace',str(directory/'trace.jsonl')],
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            client = None
            try:
                deadline = time.monotonic()+8
                while not (ipc/'data.sock').exists():
                    if server.poll() is not None:
                        raise RuntimeError(server.communicate()[1])
                    if time.monotonic()>deadline:
                        raise TimeoutError('startup')
                    time.sleep(.02)
                client = Client(ipc)
                client.frame()
                camera = cfg['cameras'][0]
                transform = np.asarray(camera['T_camera_from_vehicle'])
                optical = np.array([[(i%6-3)*.2,(i//6-1.5)*.2,3.+.2*(i%5)] for i in range(24)])
                xyz = (optical-transform[:3,3]) @ transform[:3,:3]
                uv = project(camera,xyz)
                provenance = dict(dataset_id='protocol-synthetic-v1',
                    training=dict(observation_ids=[f't-{i}' for i in range(12)],frame_ids=['train-frame']*12),
                    validation=dict(observation_ids=[f'v-{i}' for i in range(12)],frame_ids=['validation-frame']*12))
                valid = dict(camera_id=0,method='iterative',points=xyz[:12].tolist(),pixels=uv[:12].tolist(),
                    validation_points=xyz[12:].tolist(),validation_pixels=uv[12:].tolist(),provenance=provenance)
                # Include integral pixel values as doubles in the baseline; the
                # equivalent request below will encode these and XYZ zeros as ints.
                valid['pixels'][0][0] = float(round(valid['pixels'][0][0]))
                valid['validation_pixels'][0][1] = float(round(valid['validation_pixels'][0][1]))
                cases = []
                request = copy.deepcopy(valid)
                del request['provenance']
                cases.append((request,'calibration_provenance_required'))
                request = copy.deepcopy(valid)
                request['provenance']['validation']['frame_ids'][0] = 'train-frame'
                cases.append((request,'calibration_train_validation_frame_overlap'))
                request = copy.deepcopy(valid)
                request['provenance']['validation']['observation_ids'][0] = 't-0'
                cases.append((request,'calibration_duplicate_observation_id'))
                request = copy.deepcopy(valid)
                request['validation_points'][0] = request['points'][0]
                request['validation_pixels'][0] = request['pixels'][0]
                cases.append((request,'calibration_duplicate_correspondence'))
                for request,reason in cases:
                    ack = client.command('calibrate',**request)
                    self.assertFalse(ack['accepted'],ack)
                    self.assertEqual(ack['reason'],reason)
                    self.assertNotIn('job_id',ack)
                    client.frame()
                baseline = client.command('state')
                for camera_id in (-1,4,2**32,2**32+1,2**63-1,2**64-1,True,0.0):
                    with self.subTest(camera_id=camera_id):
                        request = copy.deepcopy(valid)
                        request['camera_id'] = camera_id
                        rejected = client.command('calibrate',**request)
                        self.assertFalse(rejected['accepted'],rejected)
                        expected_reason = ('camera_id_integer_required'
                                           if type(camera_id) in (bool,float) else 'camera_id_out_of_range')
                        self.assertEqual(rejected['reason'],expected_reason)
                        self.assertNotIn('job_id',rejected)
                        self.assertEqual(rejected['state_revision'],baseline['state_revision'])
                        self.assertEqual(rejected['config_revision'],baseline['config_revision'])
                        client.frame()
                for field,bad_value in (('points',True),('pixels','12.5'),
                                        ('validation_points',None),('validation_pixels',[])):
                    with self.subTest(field=field):
                        request = copy.deepcopy(valid)
                        request[field][0][0] = bad_value
                        rejected = client.command('calibrate',**request)
                        self.assertFalse(rejected['accepted'],rejected)
                        self.assertEqual(rejected['reason'],'command_number_required')
                        self.assertNotIn('job_id',rejected)
                        self.assertEqual(rejected['state_revision'],baseline['state_revision'])
                        self.assertEqual(rejected['config_revision'],baseline['config_revision'])
                        client.frame()
                ack = client.command('calibrate',**valid)
                self.assertTrue(ack['accepted'],ack)
                self.assertEqual(ack['job_id'],'calib-job-1')
                self.assertEqual(ack['validation_policy'],'client_declared_frames_and_exact_content_disjoint')
                deadline = time.monotonic()+5
                while True:
                    client.frame()
                    status = client.command('calibration_status',job_id=ack['job_id'])
                    self.assertTrue(status['accepted'],status)
                    if status['job_state'] in ('completed','failed'):
                        break
                    if time.monotonic()>deadline:
                        raise TimeoutError('calibration did not complete')
                    time.sleep(.01)
                self.assertEqual(status['job_state'],'completed',status)
                self.assertTrue(status['quality_accepted'],status)
                self.assertEqual(status['dataset_id'],provenance['dataset_id'])
                self.assertEqual(status['training_frames'],1)
                self.assertEqual(status['validation_frames'],1)
                self.assertEqual(status['training_observations'],12)
                self.assertEqual(status['validation_observations'],12)
                mixed = copy.deepcopy(valid)
                for field in ('points','pixels','validation_points','validation_pixels'):
                    mixed[field] = [[int(value) if value.is_integer() else value for value in row]
                                    for row in mixed[field]]
                    self.assertTrue(any(type(value) is int for row in mixed[field] for value in row),field)
                mixed_ack = client.command('calibrate',**mixed)
                self.assertTrue(mixed_ack['accepted'],mixed_ack)
                self.assertEqual(mixed_ack['job_id'],'calib-job-2')
                deadline = time.monotonic()+5
                while True:
                    client.frame()
                    mixed_status = client.command('calibration_status',job_id=mixed_ack['job_id'])
                    self.assertTrue(mixed_status['accepted'],mixed_status)
                    if mixed_status['job_state'] in ('completed','failed'):
                        break
                    if time.monotonic()>deadline:
                        raise TimeoutError('mixed numeric calibration did not complete')
                    time.sleep(.01)
                self.assertEqual(mixed_status['job_state'],'completed',mixed_status)
                self.assertTrue(mixed_status['quality_accepted'],mixed_status)
                for field in ('training_rmse_px','validation_rmse_px','validation_max_error_px'):
                    self.assertAlmostEqual(mixed_status[field],status[field],places=9)
            finally:
                if client:
                    client.close()
                server.terminate()
                _,stderr = server.communicate(timeout=5)
                if server.returncode not in (0,-15):
                    raise RuntimeError(stderr)


    def test_qt_client_offscreen(self):
        if not (BUILD/'sv-client').exists():self.skipTest('Qt client disabled')
        with tempfile.TemporaryDirectory(prefix='sv-qt-smoke-') as td:
            directory=Path(td);cfg=json.loads(CONFIG.read_text());manifest=generate(directory/'fixture',cfg,4);ipc=directory/'ipc'
            server=subprocess.Popen([str(BUILD/'sv-server'),'--config',str(CONFIG),'--manifest',str(manifest),'--ipc-dir',str(ipc),'--trace',str(directory/'trace.jsonl')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                deadline=time.monotonic()+8
                while not (ipc/'data.sock').exists():
                    if server.poll() is not None:raise RuntimeError(server.communicate()[1])
                    if time.monotonic()>deadline:raise TimeoutError('startup')
                    time.sleep(.02)
                environment=os.environ.copy();environment.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software')
                result=subprocess.run([str(BUILD/'sv-client'),str(ipc),'--smoke'],env=environment,text=True,capture_output=True,timeout=8)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertNotIn('QQmlApplicationEngine failed',result.stderr)
                self.assertNotIn('ReferenceError',result.stderr)
                self.assertIn('ui_receive',result.stdout,result.stderr)
                self.assertIn('ui_present_submit',result.stdout,result.stderr)
            finally:
                server.terminate();out,err=server.communicate(timeout=5)
                if server.returncode not in [0,-15]:raise RuntimeError(err)


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
