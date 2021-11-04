#!/bin/bash
flatpak-builder flatpak-build-dir --force-clean *.yml
flatpak-builder --user --install --force-clean flatpak-build-dir *.yml
flatpak run $(basename -s ".yml" *.yml)
