import React, { useEffect, useRef } from 'react';
import useState from 'react-usestateref';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableContainer from '@mui/material/TableContainer';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import Paper from '@mui/material/Paper';
import Button from '@mui/material/Button';
import Stack from '@mui/material/Stack';
import Checkbox from '@mui/material/Checkbox';
import RestartAltIcon from '@mui/icons-material/RestartAlt';
import { meros } from 'meros/browser';
import { Backend, CameraStreamingURL, MongoURL, DynamicsURL, PredictURL } from './Common';


function bytesToBase64(bytes: Uint8Array) {
  const binString = Array.from(bytes, (byte) =>
    String.fromCodePoint(byte),
  ).join("");
  return btoa(binString);
}

function sleep(ms: number) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

export default function Pickup(props: {backend: React.RefObject<Backend>}) {

  const [img, setImg, imgRef] = useState<string>("");
  const [gripperState, setGripperState, gripperStateRef] = useState<number>(0);
  const [armState, setArmState, armStateRef] = useState<number[]>([0,0,0,0,0,0]);
  const [prediction, setPrediction, predictionRef] = useState<number[] | null>();
  const [, setStopFlag, stopFlagRef] = useState(false);
  const refResetFlag = useRef<HTMLInputElement>(null);

  function predict() {
    const reset = refResetFlag.current?.checked || false;
    const payload = {
      image: imgRef.current,
      state: armStateRef.current.concat([gripperStateRef.current]),
      reset: reset,
    };

    return sleep(500).then(() =>
      fetch(
        PredictURL,
        {
          headers: {'Content-Type': 'application/json'},
          method: 'POST',
          body: JSON.stringify(payload),
        }
      )
    ).then((resp) => resp.json()).then((json) => {
      setPrediction(json);
      if (refResetFlag.current)
        refResetFlag.current.checked = false;
    });
  }

  function doit() {
    if (predictionRef.current == null) {
      console.log("no prediction yet. Nothing executed.")
      return;
    }

    const path = [{
      positions: predictionRef.current.slice(0, 6),
      gripper: predictionRef.current[6],
    }];
    console.log("executing: ", path);
    return fetch(DynamicsURL+"/execute", {
      headers: {
        'Content-Type': 'application/json'
      },
      method: 'POST',
      body: JSON.stringify({
        path: path,
      }),
    }).then((resp) => resp.json()).then((json) => predict());
  }

  const autoPickup = async () => {
    setStopFlag(false);
    var first = true;
    while (!stopFlagRef.current) {
      refResetFlag.current!.checked = first;
      await predict();
      await doit();
      first = false;
    }
  }

  const stop = () => {
    setStopFlag(true);
  }

  useEffect(() => {
    try {
      props.backend.current?.toggleCamera(true, false);
    }
    catch(e) {
      console.log(e);
    }

    console.log("Setting up a websocket for arm states.");
    const wsMongo = new WebSocket(`ws://${MongoURL}/robot/_streams/all`);
    wsMongo.onerror = console.error;
    wsMongo.onmessage = (data) => {
      if (data instanceof MessageEvent) {
        const rec = JSON.parse(data.data)
        setArmState(rec.fullDocument.position);
        setGripperState(rec.fullDocument.gripper);
      }
    };

    const controller = new AbortController();
    const retrive = async () => {

      const parts = await fetch(CameraStreamingURL, { signal: controller.signal }).then(
        (res) => meros<string>(res)
      );

      console.log("Connected to the camera")

      if (parts instanceof Response) {
        console.log("No streaming from the endpoint");
        console.log(parts);
        return;
      }
      for await (const part of parts) {
        const b64 = bytesToBase64(part.body);
        setImg(b64);
      }
    }

    retrive().catch(console.error);
    // a hook to terminate the connection once left the view.
    return () => {controller.abort();}
  }, [props.backend, setArmState, setGripperState, setImg])

  return (
    <div style={{margin: "5px 5px", display: 'flex', flexWrap: 'wrap'}}>
      {img !== ""? <img width="640" height="480" alt="from the camera" src={`data:image/jpg;base64,${img}`} />: <div /> }

      <TableContainer sx={{width: "min-content", minWidth: 200, ml: 1}} component={Paper}>
        <Table size="small" aria-label="plan">
          <TableHead>
            <TableRow>
              <TableCell align="center">J1</TableCell>
              <TableCell align="center">J2</TableCell>
              <TableCell align="center">J3</TableCell>
              <TableCell align="center">J4</TableCell>
              <TableCell align="center">J5</TableCell>
              <TableCell align="center">J6</TableCell>
              <TableCell align="center">Gripper</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            <TableRow sx={{ '&:last-child td, &:last-child th': { border: 0 } }} >
              <TableCell>{armState[0].toFixed(2)}</TableCell>
              <TableCell>{armState[1].toFixed(2)}</TableCell>
              <TableCell>{armState[2].toFixed(2)}</TableCell>
              <TableCell>{armState[3].toFixed(2)}</TableCell>
              <TableCell>{armState[4].toFixed(2)}</TableCell>
              <TableCell>{armState[5].toFixed(2)}</TableCell>
              <TableCell>{gripperState.toFixed(2)}</TableCell>
            </TableRow>
            {
              prediction &&
              <TableRow sx={{ '&:last-child td, &:last-child th': { border: 0 } }} >
                <TableCell>{prediction[0].toFixed(2)}</TableCell>
                <TableCell>{prediction[1].toFixed(2)}</TableCell>
                <TableCell>{prediction[2].toFixed(2)}</TableCell>
                <TableCell>{prediction[3].toFixed(2)}</TableCell>
                <TableCell>{prediction[4].toFixed(2)}</TableCell>
                <TableCell>{prediction[5].toFixed(2)}</TableCell>
                <TableCell>{prediction[6].toFixed(2)}</TableCell>
              </TableRow>
            }
          </TableBody>
        </Table>
      </TableContainer>

      <Stack spacing={1} direction={'column'} alignItems='stretch' sx={{ml: 1}} width="min-content">
        <Stack direction={'row'} alignItems='center' spacing={1}>
          <Checkbox id="resetFlag" icon={<RestartAltIcon color="disabled"/>} checkedIcon={<RestartAltIcon/>} inputRef={refResetFlag} />
          <Button variant="contained" onClick={predict} >Predict</Button>
          <Button variant="contained" onClick={doit} disabled={!prediction}>Go</Button>
        </Stack>

        <Button variant="contained" onClick={autoPickup} > Pickup automatically </Button>
        <Button variant="contained" onClick={stop} > Stop! </Button>
      </Stack>
    </div>
  );

}
