import React, { useState } from 'react';
import './App.css';
import Streaming from './Streaming';
import Pickup from './Pickup';
import { CapturedImage, Mode } from './Common'

function App() {

  const [mode, setMode] = useState<Mode>(Mode.View);
  const [imageQueue, setImageQueue] = useState<CapturedImage[]>([]);

  return (
    <div className='App'>
    {
      mode === Mode.View?
        <Streaming mode={mode} setMode={setMode} imageQueue={imageQueue} setImageQueue={setImageQueue}/>
      : <Pickup mode={mode} setMode={setMode} imageQueue={imageQueue}/>
    }
    </div>
  );
}

export default App;
