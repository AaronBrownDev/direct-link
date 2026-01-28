# DirectLink

Ultra-low latency remote production platform for professional film and TV production.

## What is DirectLink?

DirectLink is a real-time video streaming platform built for professional film and TV production. It allows directors to view and control multiple camera feeds remotely with low latency, eliminating the need for on-site presence and saving production companies significant travel costs.

## Milestones

The [DirectLink milestones](https://github.com/AaronBrownDev/direct-link/milestones) track the project roadmap:

- **Milestone 1: MVP** (Jan-March 2025) - Working 4-camera transmission with infrastructure
- **Milestone 2: Capstone Presentation** (April 2025) - Polish, documentation, and demo preparation

## Build Instructions

**Linux**

1. Install the packages for Qt Creator, Qt 5, and CMake.
```bash
sudo apt install -y qtcreator qtbase5-dev qt5-qmake cmake
```
2. Open Qt Creator.

```bash
qtcreator
```

3. Select Open Project, then select CMakeLists.txt in the direct-link/client/ directory.

4. If prompted, check the 'Select all kits' checkbox when configuring the project.

5. Build, then run the project.


**Windows + MacOS**

1. Create or Sign into a Qt Account (https://login.qt.io/login).

2. Download Qt Installer (https://www.qt.io/download-qt-installer).

3. Run the Installer and follow the installation steps. Select the default options where applicable.

4. Open Qt Creator.

5. Select Open Project, then select CMakeLists.txt in the direct-link/client/ directory.

6. If prompted, check the 'Select all kits' checkbox when configuring the project.

7. Build, then run the project.

## Documentation

- [Architecture Overview](docs/architecture/)
- [API Documentation](docs/api/)
- [Development Guide](docs/development/)
- [Deployment Guide](docs/deployment/)

## Team

DirectLink is being developed as a capstone project at Louisiana State University Shreveport (January-April 2025):

- **Aaron Brown** - Backend + Infrastructure
- **Courtnae James** - Backend + QA
- **Justin Williams** - Client Application
- **Ricky Wiggins Jr.** - Video Core
