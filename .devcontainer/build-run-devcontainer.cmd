REM Run devcontainer locally without VSCode
REM docker build -f Dockerfile . -t devcontainer-image:latest
docker run   -it --rm -v %~dp0/..:/workspaces -u vscode -w /workspaces --name devcontainer mcr.microsoft.com/devcontainers/dotnet:6.0-bullseye bash
