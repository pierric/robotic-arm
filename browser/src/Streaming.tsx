import React, { useEffect, useRef, useImperativeHandle , MutableRefObject} from 'react';
import { SxProps } from '@mui/system';
import Button from '@mui/material/Button';
import IconButton from '@mui/material/IconButton';
import Card from '@mui/material/Card';
import CardHeader from '@mui/material/CardHeader';
import CardContent from '@mui/material/CardContent';
import CardActions from '@mui/material/CardActions';
import Stack from '@mui/material/Stack';
import Slider from '@mui/material/Slider';
import Box from '@mui/material/Box';
import List from '@mui/material/List';
import ListItem from '@mui/material/ListItem';
import ListItemText from '@mui/material/ListItemText';
import CircleIcon from '@mui/icons-material/Circle';
import AddIcon from '@mui/icons-material/Add';
import HomeIcon from '@mui/icons-material/Home';
import SaveIcon from '@mui/icons-material/Save';
import ClearIcon from '@mui/icons-material/Clear';
import DoneIcon from '@mui/icons-material/Done';
import PhotoCameraIcon from '@mui/icons-material/PhotoCamera';
import RemoveIcon from '@mui/icons-material/Remove';
import WarningIcon from '@mui/icons-material/Warning';
import KeyboardArrowLeftIcon from '@mui/icons-material/KeyboardArrowLeft';
import KeyboardArrowRightIcon from '@mui/icons-material/KeyboardArrowRight';
import KeyboardDoubleArrowRightIcon from '@mui/icons-material/KeyboardDoubleArrowRight';
import KeyboardDoubleArrowLeftIcon from '@mui/icons-material/KeyboardDoubleArrowLeft';
import PowerSettingsNewIcon from '@mui/icons-material/PowerSettingsNew';
import Dialog from '@mui/material/Dialog';
import DialogTitle from '@mui/material/DialogTitle';
import DialogContent from '@mui/material/DialogContent';
import DialogActions from '@mui/material/DialogActions';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableContainer from '@mui/material/TableContainer';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import Paper from '@mui/material/Paper';
import mqtt from 'mqtt';
import useState from 'react-usestateref';
import 'slick-carousel/slick/slick.css';
import 'slick-carousel/slick/slick-theme.css';

import {
  SharedState, CapturedImage, MongoURL,
  MqttURL, MoonrakerURL, Hostname, DynamicsURL,
} from './Common'

interface AxisLimits {
  x: [number, number],
  y: [number, number],
  z: [number, number],
  a: [number, number],
  b: [number, number],
  c: [number, number],
}

interface AxisHasValue {
  getValue: () => number,
  setValue: (newvalue: number) => void,
  getLimit: () => [number, number] | null,
}

interface Mark {
    value: number;
    label?: React.ReactNode;
}

type AxisProps = {
  name: string,
  homed: boolean,
  step?: number,
  logscale?: boolean,
  limits?: [number, number],
  marks?: Mark[],
  onUpdate: (value: number) => void,
}

const PresetPostures: Record<string, number[]> = {
  init: [9.42, 20, -42, 0, 0, 15.7],
  Lform: [9.42, 0, -16, -0.4, 6, 15.7],
  Iform: [9.42, 0, 0, 0, 0, 15.7],
}

const Axis = React.forwardRef(function Axis({name, homed, step, logscale, limits, marks, onUpdate}: AxisProps, ref) {

  const [value, setValue, valueRef] = useState(0);
  useImperativeHandle(ref,
    () => ({
      setValue: (newvalue: number) => {setValue(newvalue);},
      getValue: () => {return valueRef.current;},
      getLimit: () => {return limits;},
    }));

  const base = 1.02;
  const logbase = Math.log(base);

  const handleClick = (event: React.SyntheticEvent | Event, value: number | number[]) => {
    if (typeof value !== "number") {
      console.log("wrong type of value, ", value);
      return;
    }
    setValue(value);
    const upd_value = logscale? Math.log(value) / logbase : value;
    onUpdate(upd_value);
  }

  const limitsProps = limits == null ? {} : {
    min: logscale? Math.pow(base, limits[0]) : limits[0],
    max: logscale? Math.pow(base, limits[1]) : limits[1],
  };

  const scaleProps = logscale == null ? {} : {
    scale: (value: number) => Math.log(value) / logbase,
  }

  return (
    <Stack spacing={3} direction={'row'} alignItems={'center'} sx={{mt: 5}}>
      <Typography id={`{name}-slider`} gutterBottom> {name} </Typography>
      <Slider aria-label={name} valueLabelDisplay="on"
        disabled={!homed}
        value={value}
        step={step || 0.1}
        onChangeCommitted={handleClick}
        marks={marks} {...scaleProps} {...limitsProps} />
    </Stack>
  );
});


type KinematicsAxisProps = {
  title: string,
  value: number,
  cardSx?: SxProps,
  update: (diff: number) => () => void,
}

function KinematicsAxis({title, value, cardSx, update}: KinematicsAxisProps) {
  return (
    <Card sx={{...cardSx}}>
      <CardContent>
        <Typography gutterBottom> {title} [{value.toFixed(2)}] </Typography>
        <IconButton aria-label="dec1" color="primary" onClick={update(0.1)}>
          <KeyboardDoubleArrowLeftIcon /></IconButton>
        <IconButton aria-label="dec2" color="secondary" onClick={update(0.01)}>
          <KeyboardArrowLeftIcon /></IconButton>
        <IconButton aria-label="inc2" color="secondary" onClick={update(-0.01)}>
          <KeyboardArrowRightIcon /></IconButton>
        <IconButton aria-label="inc1" color="primary" onClick={update(-0.1)}>
          <KeyboardDoubleArrowRightIcon  /></IconButton>
      </CardContent>
    </Card>
  )
}

type KinematicsPlanProps = {
  axes: MutableRefObject<AxisHasValue | undefined>[],
  homed: boolean,
}

type KinematicsPlan = number[][]

function KinematicsPlanC({axes, homed}: KinematicsPlanProps) {

  const [offset, setOffset] = useState([0, 0, 0]);
  const [plan, setPlan] = useState<KinematicsPlan>([])
  const [execDisabled, setExecDisabled] = useState(true);

  const upd = (axis: number) => (diff: number) => () => {
    const copy = [...offset];
    copy[axis] += diff;
    setOffset(copy);
    updatePlan();
  }
  const reset = () => {setOffset([0, 0, 0]);}

  const updatePlan = () => {
    const positions = axes.map((axis) => axis.current!.getValue())
    fetch(DynamicsURL+"/plan", {
      headers: {
        'Content-Type': 'application/json'
      },
      method: 'POST',
      body: JSON.stringify({
        q: positions,
        offset: offset,
      }),
    }).then((resp) => resp.json()).then((json) => {
      setPlan(json.path);

      setExecDisabled(true);

      if (!homed) {
        console.log("not homed.")
        return;
      }

      if (!json.arrived) {
        console.log("plan doesn't reach destination.")
        return;
      }

      const check = (value: number, index: number) => {
        const minmax = axes[index].current?.getLimit();
        console.log("checking the limit", value, minmax);
        return (minmax == null || value < minmax[0] ||  value > minmax[1]);
      }

      for (var step of json.path) {
        if (step.map(check).some((x: boolean) => x)) {
          return;
        }
      }

      setExecDisabled(false);
    })
  }

  const format = (step: number[]): string => {
    return step.map((v) => `[${v.toFixed(2)}]`).join(", ");
  }

  const execute = () => {
    const path = plan.map(positions => {return {positions: positions};});
    fetch(DynamicsURL+"/execute", {
      headers: {
        'Content-Type': 'application/json'
      },
      method: 'POST',
      body: JSON.stringify({
        path: path,
      }),
    }).then((resp) => resp.json()).then((json) => {
      reset();
    })
  }

  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', marginTop: "10px" }}>
      <Stack spacing={3} direction={'column'} alignItems={'center'}>
        <KinematicsAxis title={"X"} value={offset[0]} update={upd(0)} />
        <KinematicsAxis title={"Y"} value={offset[1]} update={upd(1)} />
        <KinematicsAxis title={"Z"} value={offset[2]} update={upd(2)} />
        <Button variant="contained" onClick={reset}>Reset</Button>
      </Stack>

      <div>
        <TableContainer sx={{width: "min-content", minWidth: 200}} component={Paper}>
          <Table size="small" aria-label="plan">
            <TableHead>
              <TableRow>
                <TableCell align="center">J1</TableCell>
                <TableCell align="center">J2</TableCell>
                <TableCell align="center">J3</TableCell>
                <TableCell align="center">J4</TableCell>
                <TableCell align="center">J5</TableCell>
                <TableCell align="center">J5</TableCell>
              </TableRow>
            </TableHead>
            <TableBody>
            {plan.map((row, index) => (
                <TableRow
                  key={index}
                  sx={{ '&:last-child td, &:last-child th': { border: 0 } }}
                >
                  <TableCell>{row[0].toFixed(2)}</TableCell>
                  <TableCell>{row[1].toFixed(2)}</TableCell>
                  <TableCell>{row[2].toFixed(2)}</TableCell>
                  <TableCell>{row[3].toFixed(2)}</TableCell>
                  <TableCell>{row[4].toFixed(2)}</TableCell>
                  <TableCell>{row[5].toFixed(2)}</TableCell>
                </TableRow>
            ))}
            </TableBody>
          </Table>
        </TableContainer>
        <Button variant="contained" sx={{mt: 1}} disabled={execDisabled} onClick={execute}>
          Execute
        </Button>
      </div>

    </div>

  )
}


type SavePathDialogProps = {
  open: boolean,
  onYes: (name: string) => void,
  onNo: () => void,
}

function SavePathDialog({open, onYes, onNo}: SavePathDialogProps) {

  const [error, setError] = useState(false);
  const valueRef = useRef<HTMLInputElement>();

  const handleOk = () => {
    const value = valueRef.current!.value;
    if (value === "") {
      setError(true);
    }
    else {
      onYes(value);
    }
  }

  const handleChange = () => {
    setError(false);
  }

  return (
    <Dialog onClose={onNo} open={open}>
      <DialogTitle>Save As</DialogTitle>
        <DialogContent>
          <TextField sx={{mt: 0.5}} id="name" label="name" variant="outlined" inputRef={valueRef} error={error} onChange={handleChange}/>
        </DialogContent>
        <DialogActions>
          <IconButton aria-label="ok" color="primary" onClick={handleOk}>
            <DoneIcon />
          </IconButton>
          <IconButton aria-label="cancel" color="primary" onClick={onNo}>
            <ClearIcon />
          </IconButton>
        </DialogActions>
    </Dialog>
  )
}

type PathStepTimestampProps = {
  timestamp: number,
  index: number,
}

function PathStepTimestamp({timestamp, index}: PathStepTimestampProps) {
  const datetime = new Date(timestamp * 1000);
  return (
      <ListItem>
        <ListItemText primary={datetime.toISOString()} />
      </ListItem>
  )
}

type PathRecorderProps = {
  ready: boolean,
}

function PathRecorder({ready}: PathRecorderProps) {

  const [openSavePath, setOpenSavePath] = useState(false);
  const [path, setPath] = useState<number[]>([]);

  const generateStepTimestamp = (element: React.ReactElement<any>) => {
    return path.map((value) =>
      React.cloneElement(element, {
        key: value,
        timestamp: value,
      }),
    );
  }

  const handleAddStep = () => {
    const timestamp = Date.now() / 1000;
    setPath([...path, timestamp]);
  }

  const openSaveDialog = () => {
    setOpenSavePath(true);
  }

  const cancelSaveDialog = () => {
    setOpenSavePath(false);
  }

  const confirmSaveDialog = (name: string) => {
    setOpenSavePath(false);
    fetch(`http://${MongoURL}/paths`, {
      headers: {
        'Content-Type': 'application/json'
      },
      credentials: 'include',
      method: 'POST',
      body: JSON.stringify({
        name: name,
        steps: path,
      }),
    });
  }

  const handleClear = () => {
    setPath([]);
  }

  return (
    <Box component="section" sx={{ flexGrow: 1, maxWidth: 230, border: "dashed black 2px" }}>
      <IconButton aria-label="add step" color="primary" onClick={handleAddStep} disabled={!ready}>
        <AddIcon />
      </IconButton>
      <IconButton aria-label="clear" color="primary" onClick={handleClear} disabled={!ready}>
        <ClearIcon />
      </IconButton>
      <IconButton aria-label="save path" color="primary" onClick={openSaveDialog} disabled={!ready}>
        <SaveIcon />
      </IconButton>
      <List dense={true}>
        {generateStepTimestamp(<PathStepTimestamp timestamp={0} index={0} />)}
      </List>
      <SavePathDialog open={openSavePath} onYes={confirmSaveDialog} onNo={cancelSaveDialog}/>
    </Box>
  )
}

export default function Streaming({imageQueue, setImageQueue, setMode}: SharedState) {

  const [armStatus, setArmStatus, armStatusRef] = useState<string>('off');
  const [axisLimits, setAxisLimits, axisLimitsRef] = useState<AxisLimits | null>(null);
  const [homed, setHomed] = useState(false);
  // const [positions, setPositions] = useState([0, 0, 0, 0, 0, 0]);
  const axisX = useRef<AxisHasValue>();
  const axisY = useRef<AxisHasValue>();
  const axisZ = useRef<AxisHasValue>();
  const axisA = useRef<AxisHasValue>();
  const axisB = useRef<AxisHasValue>();
  const axisC = useRef<AxisHasValue>();

  const runGCode = useRef<(gcode: string) => void>();

  const [cameraState, setCameraState, cameraStateRef] = useState(false);
  const toggleCamera = useRef<() => void>();
  const updateGripper = useRef<(value: number) => void>();
  const emergencyStop = useRef<() => void>();

  useEffect(() => {

    const cookie = `rh_auth="Basic YWRtaW46c2VjcmV0"; Version=1; Path=/; Domain=${Hostname}; SameSite=lax`;
    document.cookie = cookie

    fetch(`http://${MongoURL}/camera?page=1&pagesize=8`, {
      headers: {
        'Content-Type': 'application/json'
      },
      credentials: 'include',
    }).then(resp => resp.json()).then(json => {
      setImageQueue?.(json);
    });

    const wsMongo = new WebSocket(`ws://${MongoURL}/camera/_streams/all`);
    wsMongo.onerror = console.error;
    wsMongo.onmessage = (data) => {
      if (data instanceof MessageEvent) {
        const rec = JSON.parse(data.data)
        const latest = rec.fullDocument as CapturedImage;
        setImageQueue?.(hist => hist.slice(-8).concat([latest]));
      }
    };

    const mqttClient = mqtt.connect(MqttURL)
    mqttClient.on('connect', () => {
      // mqttClient.publish('/camera/command', 'off');

      toggleCamera.current = () => {
        setCameraState(!cameraStateRef.current);
        mqttClient.publish('/camera/command', cameraStateRef.current?'on':'off');
      }
      updateGripper.current = (value: number) => {
        mqttClient.publish('/manipulator/command', value.toString());
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

    const wsMoonraker = new WebSocket(MoonrakerURL);
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
          //setPositions(positions.slice(0, 6));
          axisX.current?.setValue(positions[0]);
          axisY.current?.setValue(positions[1]);
          axisZ.current?.setValue(positions[2]);
          axisA.current?.setValue(positions[3]);
          axisB.current?.setValue(positions[4]);
          axisC.current?.setValue(positions[5]);
        }
      }
    };

    return () => {
      clearInterval(intervalRoutine);
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // const handleChooseClick = () => {
  //   if (cameraState) {
  //     toggleCamera.current?.();
  //   }
  //   setMode?.(Mode.Pickup);
  // };

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

  // const settings = {
  //   dots: true,
  //   centerMode: true,
  //   centerPadding: '20px',
  //   infinite: false,
  //   speed: 500,
  //   slidesToShow: 1,
  //   rows: 1,
  //   slidesPerRow: 8
  // };

  const statusColor: string =
    armStatus !== 'ready' ? 'red':
    homed ? 'green': 'orange';

  return (
    <div>
      <div style={{ display: 'flex', flexWrap: 'wrap' }}>
        <div style={{ flexGrow: 2 }}>
          <Card>
            <CardContent>
              <Axis name="J1" homed={homed} limits={axisLimits?.x}
                onUpdate={(value) => {runGCode.current!('G1 X' + value)}} ref={axisX} />
              <Axis name="J2" homed={homed} limits={axisLimits?.y}
                onUpdate={(value) => {runGCode.current!('G1 Y' + value)}} ref={axisY} />
              <Axis name="J3" homed={homed} limits={axisLimits?.z}
                onUpdate={(value) => {runGCode.current!('G1 Z' + value)}} ref={axisZ} />
              <Axis name="J4" homed={homed} limits={axisLimits?.a}
                onUpdate={(value) => {runGCode.current!('G1 A' + value)}} ref={axisA} />
              <Axis name="J5" homed={homed} limits={axisLimits?.b}
                onUpdate={(value) => {runGCode.current!('G1 B' + value)}} ref={axisB} />
              <Axis name="J6" homed={homed} limits={axisLimits?.c}
                onUpdate={(value) => {runGCode.current!('G1 C' + value)}} ref={axisC} />
              <Axis name="M" homed={armStatus === 'ready'}
                limits={[0, 45]} logscale={true} step={0.01}
                marks={[{value: 1.22, label: '10'}, {value: 1.82, label: '30'}]}
                onUpdate={updateGripper.current!} />
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
        <PathRecorder ready={armStatus === 'ready' && homed}/>
        <div style={{ alignSelf: "center", width: "min-content", margin: "0px 20px" }}>
        {
          imageQueue.length === 0 ?
            <div>Loading...</div> :
          <div>
            <img src={`data:image/jpg;base64,${imageQueue.slice(-1)[0].image}`} alt='from camera'/>
          </div>
        }
        </div>
      </div>
      <KinematicsPlanC axes={[axisX, axisY, axisZ, axisA, axisB, axisC]} homed={homed}/>
    </div>
  );

}
