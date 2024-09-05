import React, { useEffect, useState, useRef } from 'react';
import Slider from 'react-slick';
import Button from '@mui/material/Button';
import ArrowBackIcon from '@mui/icons-material/ArrowBack';
import AdjustIcon from '@mui/icons-material/Adjust';
import { Image, Circle, Stage, Layer } from 'react-konva';
import Konva from 'konva';
import useImage from 'use-image';
import pako from 'pako';
import { Buffer } from "buffer";
import 'slick-carousel/slick/slick.css';
import 'slick-carousel/slick/slick-theme.css';

import { SharedState, Mode, EncodedImage, EncodedMask } from './Common';

interface ImageWithOverlayProps {
  image: string,
  overlay: string | null,
  points: [number, number][],
  onClick: (evt: Konva.KonvaEventObject<MouseEvent>) => void,
}

const ImageWithOverlay = ({image, overlay, points, onClick}: ImageWithOverlayProps) => {
  const [im] = useImage(`data:image/jpg;base64,${image}`);
  //const [ov] = useImage(`data:image/png;base64,${overlay}`);
  const width = im?.width ?? 640;
  const height = im?.height ?? 480;

  // the type is the raw Konva Image
  const imageRef = useRef<Konva.Image>(null);

  useEffect(() => {
    if (im) {
      imageRef.current?.cache({
        pixelRatio: 1,
      });
    }
  }, [im]);

  const applyMask = (imageData: ImageData) => {
    if (overlay) {
      const msk_bytes = Buffer.from(overlay, 'base64');
      const msk = JSON.parse(pako.inflate(msk_bytes, {to: "string"}));
      // msk is array of size width * height
      if (msk.length !== width * height) {
        console.log("WARNING: mask shape mismatch: %d != (%d, %d)", msk.length, width, height);
      }

      else if (imageData.data.length !== width * height * 4) {
        console.log("WARNING: image shape mismatch: %d != (%d, %d)", imageData.data.length, width, height);
      }

      else {
        for (var i = 0; i < imageData.data.length; i += 4) {
          const midx = Math.trunc(i / 4)
          const fg = msk[midx] / 255 * 0.6;
          const bg = 1 - fg;
          imageData.data[i] = Math.trunc(imageData.data[i] * bg);
          imageData.data[i+1] = Math.trunc(imageData.data[i+1] * bg + 255 * fg);
          imageData.data[i+2] = Math.trunc(imageData.data[i+2] * bg);
        }
      }
    }
  };

  return (
    <Stage width={width} height={height}>
      <Layer>
        <Image ref={imageRef} onClick={onClick} image={im}
          filters={[applyMask]}
        />
      </Layer>
      <Layer>
      {
        points.map(([x, y], idx) => (
          <Circle key={idx} x={x} y={y} radius={10} fill='red' stroke={'black'}/>
        ))
      }
      </Layer>
    </Stage>
  );
};


export default function Pickup({mode, setMode, imageQueue}: SharedState) {

  const [depthMasks, setDepthMasks] = useState<EncodedImage[]>([]);
  const [segMasks, setSegMasks] = useState<EncodedMask|null>(null);
  const [points, setPoints] = useState<[number, number][]>([]);

  const handleBackClick = () => {
    setMode?.(Mode.View);
  };

  const handleCenterClick = () => {

  };

  const addPoint = (evt: Konva.KonvaEventObject<MouseEvent>) => {
    console.log("click add point")
    setPoints(points.concat([[evt.evt.offsetX, evt.evt.offsetY]]))
  };

  useEffect(() => {
    const depthAnythingEndpoint = 'http://localhost:8000/depth';
    Promise.all(
      imageQueue.map((item) =>
        fetch(
          depthAnythingEndpoint,
          {
            headers: {'Content-Type': 'application/json'},
            method: 'POST',
            body: JSON.stringify({image: item.image}),
          }
        ).then((resp) => resp.json()).then((json) => json.image as EncodedMask)
      )
    ).then((masks) => setDepthMasks(masks));
  }, [imageQueue])

  useEffect(() => {
    if (points.length === 0) {
      return;
    }
    const samEndpoint = 'http://localhost:8000/mask';
    const img = imageQueue.slice(-1)[0].image;
    const payload = JSON.stringify({points: points, image: img, encoding: ".gz"});
    fetch(
      samEndpoint,
      {
        headers: {'Content-Type': 'application/json'},
        method: 'POST',
        body: payload,
      }
    ).then((resp) => resp.json()).then((json) => setSegMasks(json.image as EncodedImage))
  }, [points, imageQueue])

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

  const buttonStyle = {
    marginLeft: 10,
    marginRight: 10,
  }

  return (
    <div>
      <div style={{display: 'inline-block'}}>
        <ImageWithOverlay
          image={imageQueue.slice(-1)[0].image}
          overlay={segMasks}
          points={points}
          onClick={addPoint}
        />
      </div>
      <div>
        <Slider {...settings}>
        {
          imageQueue.map((cimg, idx: number) => {
            const img = `data:image/png;base64,${cimg.image}`;
            const msk = `data:image/png;base64,${depthMasks[idx]}`;
            return (
              <div key={idx}>
                <img src={img} alt='from camera' width={200}/>
                <img src={msk} alt='mask' width={200}/>
                <span>{cimg.timestamp}</span>
              </div>
            );
          })
        }
        </Slider>

        <div style={{ marginTop: '40px' }}>
        <Button variant="outlined" startIcon={<ArrowBackIcon />}
          style={buttonStyle}
          onClick={handleBackClick}>
          Back
        </Button>

        <Button variant="contained" startIcon={<AdjustIcon />}
          style={buttonStyle}
          onClick={handleCenterClick}>
          Center
        </Button>
        </div>
      </div>
    </div>
  );

}
