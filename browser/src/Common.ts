import React from 'react';
import { Dispatch, SetStateAction } from 'react';
import mqtt, { MqttClient } from 'mqtt';

export type EncodedImage = string
export type EncodedMask = string

export type CapturedImage = {
    timestamp: number;
    image: EncodedImage;
}

export const Hostname = window.location.hostname
export const KlipperHost = '192.168.178.34';
export const CameraHost = '192.168.178.98';
export const PredictURL = `http://${Hostname}:8000/predict`;
export const CameraStreamingURL = `http://${CameraHost}`;
export const MongoURL = `${Hostname}:8080`;
export const MqttURL = `ws://${Hostname}:8883`;
export const MoonrakerURL = `ws://${KlipperHost}:7125/websocket`;
export const DynamicsURL = `http://${Hostname}:8003`;

export interface MachineState {
    ready: boolean,
    setReady: React.Dispatch<React.SetStateAction<boolean>>,
    homed: boolean,
    setHomed: React.Dispatch<React.SetStateAction<boolean>>,
}

export type MoonrakerMessageHandler = (record: any) => void
export type BackendProps = MachineState
export type BackendState = {}

export class Backend extends React.Component<BackendProps , {}> {
    moonraker: WebSocket
    mqttClient: MqttClient
    intervalRoutine: any

    moonrakerHandlers: { [index: number]: MoonrakerMessageHandler }

    constructor(props: BackendProps) {
        super(props);

        this.moonraker = new WebSocket(MoonrakerURL);
        this.mqttClient = mqtt.connect(MqttURL);
        this.moonrakerHandlers = {}
    }

    componentDidMount() {
        this.intervalRoutine = setInterval(() => {
            if (this.moonraker.readyState === WebSocket.OPEN) {
                this.moonraker.send(JSON.stringify({
                    jsonrpc: '2.0',
                    method: 'server.info',
                    id: 100,
                }));

                if (this.props.ready) {
                    this.moonraker.send(JSON.stringify({
                        jsonrpc: '2.0',
                        method: 'printer.objects.query',
                        params: {
                            objects: {
                                toolhead: ['homed_axes'],
                            }
                        },
                        id: 101
                    }));
                }
            }
        }, 2000);

        this.moonraker.onmessage = (data) => {
            if (data instanceof MessageEvent) {
                const rec = JSON.parse(data.data)
                if (rec.id === 100) {
                    this.props.setReady(rec.result.klippy_state == "ready");
                }
                else if (rec.id === 101) {
                    this.props.setHomed(rec.result.status.toolhead.homed_axes === 'xyzabc');
                }
                else if (rec.id in this.moonrakerHandlers) {
                    this.moonrakerHandlers[rec.id](rec);
                }
            }
        };

        this.mqttClient.on('message', (topic, message) => {
            console.log(topic.toString(), message.toString());
        });
    }

    componentWillUnmount() {
        clearInterval(this.intervalRoutine);
    }

    runGCode(gcode: string) {
        this.moonraker.send(JSON.stringify({
            jsonrpc: '2.0',
            method: 'printer.gcode.script',
            params: {
                script: gcode
            },
            id: 220
        }));
    }
    emergencyStop() {
        this.moonraker.send(JSON.stringify({
            "jsonrpc": "2.0",
            "method": "printer.emergency_stop",
            "id": 999
        }));
    }
    toggleCamera(enable?:boolean, record?:boolean) {
        if (!enable) {
            record = false;
        }
        this.mqttClient.publish('/camera/record', record?"on":"off");
        this.runGCode("SET_PIN PIN=camera_en VALUE=" + (enable?1:0).toString());
    }

    sendMoonrakerMessage(msg: string) {
        this.moonraker.send(msg);
    }

    setMoonrakerMessageHandler(id: number, handler: MoonrakerMessageHandler)
        : MoonrakerMessageHandler | null {
        const old_handler = this.moonrakerHandlers[id] || null;
        this.moonrakerHandlers[id] = handler;
        return old_handler;
    }

    delMoonrakerMessageHandler(id: number): MoonrakerMessageHandler | null {
        const old_handler = this.moonrakerHandlers[id] || null;
        delete this.moonrakerHandlers[id];
        return old_handler;
    }

    render() {
        return null;
    }

};

export function gripperStateToAngle(state: number): number {
    /*
    # as per parol6_octpus.cfg, the maximum_servo_angle is
    # 90, the value that we get from the klipper is half of
    # the pulse width (strangely in unit of 10ms).
    # That is 0° <-> 0.045, and 90° <-> 0.125
    */
    return (Math.max(0.045, Math.min(state, 0.125)) - 0.045) * 1125
}
