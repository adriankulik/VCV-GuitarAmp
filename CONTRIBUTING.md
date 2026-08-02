# Contributing to Guitar Amp & Effects

First off, thank you for considering contributing to this plugin! It's people like you that make the open source community such a great place to learn, inspire, and create.

## How to contribute

### 1. Fork the repo and create your branch
- Fork the repository to your own GitHub account.
- Clone the project to your machine.
- Create a branch locally with a succinct but descriptive name (e.g., `feature/new-chorus-pedal` or `bugfix/shimmer-noise`).

### 2. Setup your development environment
Ensure you have the VCV Rack 2 SDK set up. Refer to the [README.md](README.md) for detailed instructions on setting up your `RACK_DIR`. 

### 3. Making changes
- Try to keep your code style consistent with the rest of the codebase.
- Use meaningful variable names and leave comments explaining complex DSP logic.
- If you're altering the UI (`GuitarAmp.svg` or `LogoLight.svg`), please make sure you strip out any unsupported Figma tags like `<mask ...>` or `<clipPath ...>`, as NanoSVG does not support them.

### 4. Testing
Compile and run the plugin locally:
```bash
./build.sh
```
Ensure your changes do not crash VCV Rack and that CPU usage remains reasonable. 

### 5. Open a Pull Request
- Push your branch to your fork.
- Open a Pull Request against the `main` branch of this repository.
- Fill out the PR template completely.
- Once submitted, GitHub Actions will automatically run the `pr.yml` workflow to ensure the plugin builds successfully across platforms.

## Code of Conduct
Please note that this project is released with a Contributor Code of Conduct. By participating in this project you agree to abide by its terms.
