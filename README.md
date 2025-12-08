# hyprshutdown
A graceful shutdown/logout utility for Hyprland, which prevents apps from crashing / dying unexpectedly.

![](./assets/preview.png)

## NixOS Installation with Home-Manager

Add these inputs to your `flake.nix`.

```nix
{
    inputs = {
        
        home-manager = {
          url = "github:nix-community/home-manager";
          inputs.nixpkgs.follows = "nixpkgs";
        };
        hyprland.url = "github:hyprwm/Hyprland";
        hyprshutdown.url = "github:hyprwm/hyprshutdown";
    };

    outputs = { self, nixpkgs, ... }@inputs:
    {
        ...
    };
}
```

Add the package to your `home.nix`.

```nix
{ pkgs, inputs, ... }:
{

    ...
    
    home.packages = with pkgs; [
        inputs.hyprshutdown.packages.${pkgs.stdenv.hostPlatform.system}.default
    ];
    
    ...

}
```

## Usage

Just run `hyprshutdown`. This will close all apps and exit Hyprland.

See `hyprshutdown -h` for more information.

### Notes

`hyprshutdown` does **not** shut down the system, it only shuts down Hyprland.

`hyprshutdown` does not work with anything other than Hyprland, as it relies on Hyprland IPC.
