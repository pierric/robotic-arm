import React, { useEffect, useRef } from 'react';
import Button from '@mui/material/Button';
import IconButton from '@mui/material/IconButton';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import CardActions from '@mui/material/CardActions';
import Stack from '@mui/material/Stack';
import Slider from '@mui/material/Slider';
import CircleIcon from '@mui/icons-material/Circle';
import HomeIcon from '@mui/icons-material/Home';
import PhotoCameraIcon from '@mui/icons-material/PhotoCamera';
import WarningIcon from '@mui/icons-material/Warning';
import PowerSettingsNewIcon from '@mui/icons-material/PowerSettingsNew';
import Typography from '@mui/material/Typography';
import ImageSlider from 'react-slick';
import mqtt from 'mqtt';
import useState from 'react-usestateref';
import 'slick-carousel/slick/slick.css';
import 'slick-carousel/slick/slick-theme.css';

import { SharedState, CapturedImage, Mode } from './Common'

interface AxisLimits {
  x: [number, number],
  y: [number, number],
  z: [number, number],
  a: [number, number],
  b: [number, number],
  c: [number, number],
}

type AxisProps = {
  name: string,
  homed: boolean,
  value: number,
  limits?: [number, number],
  runGCode: (gcode: string) => void,
}

const PresetPostures: Record<string, number[]> = {
  init: [9.42, 20, -42, 0, 0, 15.7],
  Lform: [9.42, 0, -16, -0.4, 6, 15.7],
  Iform: [9.42, 0, 0, 0, 0, 15.7],
}

function Axis({name, homed, value, limits, runGCode}: AxisProps) {

  const limitsProps = limits? {min: limits[0], max: limits[1]} : {};

  const handleClick = (event: React.SyntheticEvent | Event, value: number | number[]) => {
    const cmd = 'G1 ' + name + value;
    runGCode(cmd);
  }

  return (
    <Stack spacing={3} direction={'row'} alignItems={'center'} sx={{mt: 5}}>
      <Typography id={`{name}-slider`} gutterBottom> {name} </Typography>
      <Slider aria-label={name} valueLabelDisplay="on"
        disabled={!homed}
        value={value}
        step={0.1}
        marks
        onChangeCommitted={handleClick}
        {...limitsProps} />
    </Stack>
  );
}

export default function Streaming({imageQueue, setImageQueue, setMode}: SharedState) {

  const mqttURL = 'ws://localhost:8883';
  const mongoURL = 'localhost:8080';
  const moonrakerURL = 'ws://192.168.178.34:7125/websocket'

  const [armStatus, setArmStatus, armStatusRef] = useState<string>('off');
  const [axisLimits, setAxisLimits, axisLimitsRef] = useState<AxisLimits | null>(null);
  const [homed, setHomed] = useState(false);
  const [positions, setPositions] = useState([0, 0, 0, 0, 0, 0]);

  const runGCode = useRef<((gcode: string) => void) | null>(null);

  const [cameraState, setCameraState, cameraStateRef] = useState(false);
  const toggleCamera = useRef<((() => void) | null)>(null);
  const emergencyStop = useRef<((() => void) | null)>(null);

  useEffect(() => {
    document.cookie = 'rh_auth="Basic YWRtaW46c2VjcmV0"; Version=1; Path=/; Domain=localhost; Secure; SameSite=Strict';

    fetch(`http://${mongoURL}/camera?page=1&pagesize=8`, {
      headers: {
        'Content-Type': 'application/json'
      },
      credentials: 'include',
    }).then(resp => resp.json()).then(json => {
      setImageQueue?.(json);
    });

    const wsMongo = new WebSocket(`ws://${mongoURL}/camera/_streams/all`);
    wsMongo.onerror = console.error;
    wsMongo.onmessage = (data) => {
      if (data instanceof MessageEvent) {
        const rec = JSON.parse(data.data)
        const latest = rec.fullDocument as CapturedImage;
        setImageQueue?.(hist => hist.slice(-8).concat([latest]));
      }
    };

    const mqttClient = mqtt.connect(mqttURL)
    mqttClient.on('connect', () => {
      mqttClient.publish('/camera/command', 'off');

      toggleCamera.current = () => {
        setCameraState(!cameraStateRef.current);
        mqttClient.publish('/camera/command', cameraStateRef.current?'on':'off');
      }
      mqttClient.subscribe('/camera/command', (err) => {
        if (err) {
          console.log(err);
        }
      });
    });

    mqttClient.on('message', (topic, message) => {
      console.log(topic.toString(), message.toString());
    });

    const wsMoonraker = new WebSocket(moonrakerURL);
    const intervalRoutine = setInterval(() => {
      if (wsMoonraker.readyState === WebSocket.OPEN) {
        wsMoonraker.send(JSON.stringify({
          jsonrpc: '2.0',
          method: 'server.info',
          id: 100,
        }));

        if (armStatusRef.current === 'ready') {
          wsMoonraker.send(JSON.stringify({
            jsonrpc: '2.0',
            method: 'printer.objects.query',
            params: {
              objects: {
                gcode_move: null,
                toolhead: ['homed_axes', 'axis_minimum', 'axis_maximum', 'position']
              }
            },
            id: 101
          }));

          runGCode.current = (gcode) => {
            wsMoonraker.send(JSON.stringify({
              jsonrpc: '2.0',
              method: 'printer.gcode.script',
              params: {
                script: gcode
              },
              id: 200
            }));
          }

          emergencyStop.current = () => {
            wsMoonraker.send(JSON.stringify({
              "jsonrpc": "2.0",
              "method": "printer.emergency_stop",
              "id": 999
            }));
          }
        }
      }
    }, 2000);
    wsMoonraker.onmessage = (data) => {
      if (data instanceof MessageEvent) {
        const rec = JSON.parse(data.data)
        if (rec.id === 100) {
          setArmStatus(rec.result.klippy_state);
        }
        else if (rec.id === 101) {
          setHomed(rec.result.status.toolhead.homed_axes === 'xyzabc');

          if (axisLimitsRef.current === null) {
            const mins = rec.result.status.toolhead.axis_minimum;
            const maxs = rec.result.status.toolhead.axis_maximum;
            setAxisLimits({
              x: [mins[0], maxs[0]],
              y: [mins[1], maxs[1]],
              z: [mins[2], maxs[2]],
              a: [mins[3], maxs[3]],
              b: [mins[4], maxs[4]],
              c: [mins[5], maxs[5]],
            });
          }

          const positions = rec.result.status.toolhead.position;
          setPositions(positions.slice(0, 6));
        }
      }
    };

    return () => {
      clearInterval(intervalRoutine);
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const handleChooseClick = () => {
    if (cameraState) {
      toggleCamera.current?.();
    }
    setMode?.(Mode.Pickup);
  };

  const handleHome = () => {
    runGCode.current?.("SET_FAN_SPEED FAN=fan1 SPEED=1\nSET_FAN_SPEED FAN=fan2 SPEED=1\nG28");
  }

  const handleCamera = () => {
    toggleCamera.current?.();
  }

  const handlePowerDown = () => {
    runGCode.current?.("M18");
  }

  const handleStop = () => {
    emergencyStop.current?.();
  }

  const handlePreset = (idx: string) => () => {
    const pos = PresetPostures[idx];
    const names = 'XYZABC';
    const cmd = 'G1 ' + pos.map((val, idx) => names[idx] + val).join(' ');
    runGCode.current?.(cmd);
  }

  const settings = {
    dots: true,
    centerMode: true,
    centerPadding: '20px',
    infinite: false,
    speed: 500,
    slidesToShow: 1,
    rows: 1,
    slidesPerRow: 8
  };

  const statusColor: string =
    armStatus !== 'ready' ? 'red':
    homed ? 'green': 'orange';

  return (
    <div style={{ display: 'flex' }}>
      <div style={{ width: '50%' }}>
        <Card>
          <CardContent>
            <Axis name="X" homed={homed} value={positions[0]} limits={axisLimits?.x} runGCode={runGCode.current!} />
            <Axis name="Y" homed={homed} value={positions[1]} limits={axisLimits?.y} runGCode={runGCode.current!} />
            <Axis name="Z" homed={homed} value={positions[2]} limits={axisLimits?.z} runGCode={runGCode.current!} />
            <Axis name="A" homed={homed} value={positions[3]} limits={axisLimits?.a} runGCode={runGCode.current!} />
            <Axis name="B" homed={homed} value={positions[4]} limits={axisLimits?.b} runGCode={runGCode.current!} />
            <Axis name="C" homed={homed} value={positions[5]} limits={axisLimits?.c} runGCode={runGCode.current!} />
          </CardContent>
          <CardActions sx={{flex: '1', flexDirection: 'row-reverse'}}>
            <CircleIcon sx={{margin: '8px', color: statusColor}} />
            <IconButton aria-label="camera" color="primary" onClick={handleCamera}>
              <PhotoCameraIcon />
            </IconButton>
            <IconButton aria-label="stop" disabled={armStatus !== "ready"} color="error" onClick={handleStop}>
              <WarningIcon />
            </IconButton>
            <IconButton aria-label="shutdown" disabled={armStatus !== "ready"} color="primary" onClick={handlePowerDown}>
              <PowerSettingsNewIcon />
            </IconButton>
            <span style={{paddingLeft: 8}}>|</span>
            <Button variant="outlined"
              disabled={armStatus !== "ready"}
              onClick={handlePreset('Lform')}>1</Button>
            <Button variant="outlined"
              disabled={armStatus !== "ready"}
              onClick={handlePreset('init')}>0</Button>
            <IconButton aria-label="home" disabled={armStatus !== "ready" || homed} color="success" onClick={handleHome}><HomeIcon /></IconButton>
          </CardActions>
        </Card>
      </div>
      <div style={{ flexGrow: 1 }}>
      {
        imageQueue.length === 0 ?
          <div>Loading...</div> :
        <div>
          <img src={`data:image/jpg;base64,${imageQueue.slice(-1)[0].image}`} alt='from camera'/>
          <div style={{ marginTop: '40px' }}>
            <Button variant="contained" onClick={handleChooseClick}>Pick up something</Button>
          </div>
        </div>
      }
      </div>
    </div>
  );

}
