# Autonomous AI Robot - TODO List

## Phase 1: Hardware Procurement (Week 1)
- [ ] Order Raspberry Pi 4 (8GB) + case + power supply
- [ ] Order Intel RealSense D435i
- [ ] Order TB6612FNG motor drivers x2
- [ ] Order DC Gear Motors w/ encoders x2 (12V, 200 RPM)
- [ ] Order MG996R servos x2, MG90S x1
- [ ] Order Ultrasonic sensors x4 (HC-SR04)
- [ ] Order LiPo 11.1V 5000mAh + charger
- [ ] Order USB host shield for ESP32
- [ ] Order Aluminum extrusion chassis kit
- [ ] Order 5-DOF robotic arm (aluminum)
- [ ] Order mechanical components (wheels, mounts, etc.)
- [ ] Order voltage regulators, wires, connectors, etc.

## Phase 2: Environment Setup (Week 1-2)
- [ ] Set up RPi 4 with Ubuntu 22.04 + ROS2 Humble
- [ ] Install YOLOv8, OpenCV, MediaPipe, RealSense SDK
- [ ] Install OpenCode CLI + Ollama on Windows PC
- [ ] Set up VS Code development environment

## Phase 3: ESP32 Firmware Rewrite (Week 2-4)
- [ ] Rewrite esp32_opencode_chat.ino with FreeRTOS
- [ ] Create motor_controller.h/.cpp (PWM + PID + encoder)
- [ ] Create servo_controller.h/.cpp (Arm/gripper control)
- [ ] Create sensor_manager.h/.cpp (IMU, distance, line sensors)
- [ ] Create safety_monitor.h/.cpp (Emergency stop, limits, current)
- [ ] Create websocket_server.h/.cpp (Real-time PC communication)
- [ ] Test basic motor control from ESP32
- [ ] Test encoder feedback accuracy
- [ ] Test obstacle detection triggers stop

## Phase 4: Companion PC Setup (Week 3-6)
- [ ] Create vision/ module (Camera + depth + object detection)
- [ ] Create navigation/ module (SLAM / path planning)
- [ ] Create ai_reasoning/ module (ReAct loop + tool registry)
- [ ] Create memory/ module (Vector DB + SQLite task history)
- [ ] Create skills/ module (Reusable task sequences)
- [ ] Create websocket_client.py (ESP32 communication)
- [ ] Test object detection (YOLOv8 / MediaPipe)
- [ ] Test depth-based obstacle avoidance
- [ ] Test 3D point cloud processing
- [ ] Test object pose estimation
- [ ] Test human detection + tracking (MediaPipe Pose)

## Phase 5: Bridge Server Extensions (Week 4-6)
- [ ] Extend server.js for vision data streaming
- [ ] Update config.json for vision model, companion PC endpoints
- [ ] Add WebSocket support for real-time updates

## Phase 6: Web UI Extensions (Week 5-7)
- [ ] Add camera feed display
- [ ] Add robot status dashboard
- [ ] Add task controls (start/stop/pause)
- [ ] Add skill library browser
- [ ] Add human interaction controls (follow me, come here)

## Phase 7: Integration Testing (Week 7-10)
- [ ] Navigate to known waypoint
- [ ] Detect and grasp specific object
- [ ] Pick up tomato → put in basket (end-to-end)
- [ ] Avoid obstacles during navigation
- [ ] Emergency stop during motion
- [ ] Low battery return to base
- [ ] Follow person command
- [ ] Respond to "come here"

## Phase 8: Safety & Learning (Week 10-12)
- [ ] Emergency stop response time < 100ms
- [ ] Motor stall detection < 500ms
- [ ] Over-current protection
- [ ] Thermal shutdown
- [ ] Battery low warning < 20%
- [ ] Task history logging
- [ ] Feedback-based improvement (reward/penalty)
- [ ] Demonstration learning (human-in-the-loop)
- [ ] Skill generalization (similar objects)

## Phase 9: Final Testing (Week 11-12)
- [ ] Full human-robot interaction
- [ ] Learning from demonstrations
- [ ] Skill generalization to new objects
- [ ] Multi-robot coordination (optional)

---
*Last updated: $(date)*