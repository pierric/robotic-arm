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

