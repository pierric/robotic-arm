import { Dispatch, SetStateAction } from 'react';

export type EncodedImage = string
export type EncodedMask = string

export type CapturedImage = {
    timestamp: number;
    image: EncodedImage;
}

export enum Mode {
  View = 1,
  Pickup,
}

export interface SharedState {
  imageQueue: CapturedImage[],
  setImageQueue?: Dispatch<SetStateAction<CapturedImage[]>>,
  mode: Mode,
  setMode?: Dispatch<SetStateAction<Mode>>,
}

const hostname = window.location.hostname
export const CameraStreamingURL = 'http://192.168.178.93';
export const MongoURL = `${hostname}:8080`;
export const MqttURL = `ws://${hostname}:8883`;
export const MoonrakerURL = 'ws://192.168.178.34:7125/websocket'
