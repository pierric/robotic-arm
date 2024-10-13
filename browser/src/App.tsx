import React, { useEffect, useState, useRef } from 'react';
import AppBar from '@mui/material/AppBar';
import Box from '@mui/material/Box';
import Toolbar from '@mui/material/Toolbar';
import Typography from '@mui/material/Typography';
import Button from '@mui/material/Button';
import IconButton from '@mui/material/IconButton';
import TuneIcon from '@mui/icons-material/Tune';
import BackHandOutlinedIcon from '@mui/icons-material/BackHandOutlined';
import './App.css';
import Streaming from './Streaming';
import Pickup from './Pickup';
import { Backend, BackendProps } from './Common';


enum Mode {
  Control = 1,
  Pickup,
}


interface AppSwitchBarProps {
  setMode: React.Dispatch<React.SetStateAction<Mode>>
}

function AppSwitchBar({setMode}: AppSwitchBarProps) {

  const switchMode = (mode: Mode) => () => {
    setMode(mode);
  }

  return (
    <Box sx={{ flexGrow: 1 }}>
      <AppBar position="static">
        <Toolbar>
          <IconButton size="large" edge="start" color="inherit" aria-label="menu" sx={{ mr: 2 }}
            onClick={switchMode(Mode.Control)} >
            <TuneIcon />
          </IconButton>
          <IconButton size="large" edge="start" color="inherit" aria-label="menu" sx={{ mr: 2 }}
            onClick={switchMode(Mode.Pickup)} >
            <BackHandOutlinedIcon />
          </IconButton>
        </Toolbar>
      </AppBar>
    </Box>
  );
}

function App() {

  const [mode, setMode] = useState<Mode>(Mode.Control);
  const [homed, setHomed] = useState<boolean>(false);
  const [ready, setReady] = useState<boolean>(false);
  const backendRef = useRef<Backend>(null);
  //const backend = React.createElement<BackendProps, Backend, React.ComponentClass<BackendProps>>(
  //  Backend, {ref: backendRef }
  //);

  return (
    <div className='App'>
      <Backend homed={homed} ready={ready} setHomed={setHomed} setReady={setReady} ref={backendRef} />
      <AppSwitchBar setMode={setMode} />
      {
        mode === Mode.Control?
        <Streaming ready={ready} homed={homed} backend={backendRef} /> :
        <Pickup backend={backendRef} />
      }
    </div>
  );
}

export default App;
