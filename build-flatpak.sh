#!/bin/bash
flatpak-builder flatpak-build-dir --force-clean io.github.com.igorlogius.scr2ppm.yml
flatpak-builder --user --install --force-clean flatpak-build-dir io.github.com.igorlogius.scr2ppm.yml
flatpak run io.github.com.igorlogius.scr2ppm

