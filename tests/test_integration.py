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
                optical = np.array([[(i%6-2.5)*.2,(i//6-1.5)*.2,3.+.2*(i%5)] for i in range(24)])
                xyz = (optical-transform[:3,3]) @ transform[:3,:3]
                uv = project(camera,xyz)
                provenance = dict(dataset_id='protocol-synthetic-v1',
                    training=dict(observation_ids=[f't-{i}' for i in range(12)],frame_ids=['train-frame']*12),
                    validation=dict(observation_ids=[f'v-{i}' for i in range(12)],frame_ids=['validation-frame']*12))
                valid = dict(camera_id=0,method='iterative',points=xyz[:12].tolist(),pixels=uv[:12].tolist(),
                    validation_points=xyz[12:].tolist(),validation_pixels=uv[12:].tolist(),provenance=provenance)
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
