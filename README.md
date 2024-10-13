# An opensource solution for a desktop robotic arm

It is built on top of several things:
- [Parol6](https://github.com/PCrnjak/PAROL6-Desktop-robot-arm) desktop robotic arm (without the pneumatic gripper and mainboard)
- BigTreeTech Octopus V1.1
- Raspberry Pi 3B
- 24V 300W Power supply (6 stepper motors, each of which may consume up to 50W)
- 3D printer firmware and software [Klipper](https://www.klipper3d.org/). Modified to support 6-DOF as in this [repo](https://github.com/pierric/klipper/)
- (M5 Timer Camera F)[https://docs.m5stack.com/en/unit/timercam_f]
- Gripper from the (Thor project)[https://github.com/AngelLM/Thor]

## Some build instructions
- Build the parol6, without the mainboard and gripper
- Build the Thor gripper
- Print the (stl/gripper-holder-m5-gripper_m5_bridge.stl) to connect the two part
- Mount the M5 Camera on the bridge
- Print the (stl/Mainboard.stl), and place the BTT Octopus inside
- Mount two fans on the mainboard box. Otherwise, the stepper motor will very soon be too hot and stop working.
- Connect the mainboard to the power supply.
- The 4-wire connection between the gripper and the mainboard is different from the parol project. I didn't use it as a canbus. Instead, 3 wires are
  used directly to control the servo. And 1-wire to turn on/off the camera (it has internally a battery).
- Connect the 3 wires from gripper's servo to mainboard. You can plug the 5V+GND to the fan port, and the signal wire to the DIAG-6 port, as I do.
  Klipper can configurate the DIAG-6 port as PWM enabled and therefore control the servo via Klipper like other stepper motors.

## Running the software

### Mainboard side
- Klipper firmware

### RPi side
- Klipper service
- state_sync, for dumping the states of all motors to the mongodb

### Desktop side
- mongodb: database for arm's states along the time and also images from the camera
- restheart: restful endpoint for the mongodb
- mosquitto: mqtt broker.
- image_stream_dumper: read the streaming images from the camera and save them in the mongo db
- arm_dynamics:
  - endpoint for planning for arm joints with dynamic kinematics.
  - endpoint for executing a plan

Create a **.env** file along side with the **docker-compose.yaml** file.
```
DATA_STORE=<path for storing the mongodb data>
RESTHEART_TOKEN=<restheart access token>
GRIPPER_HOST=<ip of the gripper>
KLIPPER_HOST=<ip of the RPi>
```
Then run the command to bring the services.
```
docker-compose up -d
```

The **browser** folder contains a simple react app that can be used to control the arm. It has some features
- Tab 1
  - see what the camera sees
  - move the joints directly
  - move the join by specifying the offset in the end-point of the gripper. **arm_dynamics** will compute a plan
    to achive the goal.
- Tab 2
  - Control the arm with the help of a model.
 
## Training a model

I explored possibilities to build a end-to-end model to control the arm. Thanks to the huggingface (lerobot)[https://github.com/huggingface/lerobot]
project, it can be done with managable efforts as an hobby. I sketch some rough steps to train a pick-up model.

- Control the arm and collect the key joint states along the time. Save them (in mongodb)
- Collect about 120 such sucessfull runs
- Make mp4 from what the camera records for each run
- Make a lerobot dataset to include the key joint states. Each episode is states + mp4 for a run.
- Train a model. I used the **act** model, trained for 8000 steps, with chunk-size 20 (interval of 0.25 second for samples)
- The model worked very well to instruct the arm to pick up my little wood building block!
