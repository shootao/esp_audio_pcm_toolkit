---
name: PCM Monitor Logic
colors:
  surface: '#f8f9fa'
  surface-dim: '#d9dadb'
  surface-bright: '#f8f9fa'
  surface-container-lowest: '#ffffff'
  surface-container-low: '#f3f4f5'
  surface-container: '#edeeef'
  surface-container-high: '#e7e8e9'
  surface-container-highest: '#e1e3e4'
  on-surface: '#191c1d'
  on-surface-variant: '#5c403c'
  inverse-surface: '#2e3132'
  inverse-on-surface: '#f0f1f2'
  outline: '#916f6b'
  outline-variant: '#e5bdb8'
  surface-tint: '#bd1112'
  primary: '#b90c10'
  on-primary: '#ffffff'
  primary-container: '#de2e26'
  on-primary-container: '#fffbff'
  inverse-primary: '#ffb4aa'
  secondary: '#4e6073'
  on-secondary: '#ffffff'
  secondary-container: '#cfe2f9'
  on-secondary-container: '#526478'
  tertiary: '#006387'
  on-tertiary: '#ffffff'
  tertiary-container: '#007da9'
  on-tertiary-container: '#fbfcff'
  error: '#ba1a1a'
  on-error: '#ffffff'
  error-container: '#ffdad6'
  on-error-container: '#93000a'
  primary-fixed: '#ffdad5'
  primary-fixed-dim: '#ffb4aa'
  on-primary-fixed: '#410001'
  on-primary-fixed-variant: '#930007'
  secondary-fixed: '#d1e4fb'
  secondary-fixed-dim: '#b5c8df'
  on-secondary-fixed: '#091d2e'
  on-secondary-fixed-variant: '#36485b'
  tertiary-fixed: '#c4e7ff'
  tertiary-fixed-dim: '#7bd0ff'
  on-tertiary-fixed: '#001e2c'
  on-tertiary-fixed-variant: '#004c69'
  background: '#f8f9fa'
  on-background: '#191c1d'
  surface-variant: '#e1e3e4'
typography:
  headline-lg:
    fontFamily: JetBrains Mono
    fontSize: 24px
    fontWeight: '700'
    lineHeight: 32px
    letterSpacing: -0.02em
  headline-md:
    fontFamily: JetBrains Mono
    fontSize: 18px
    fontWeight: '600'
    lineHeight: 24px
  body-md:
    fontFamily: Inter
    fontSize: 14px
    fontWeight: '400'
    lineHeight: 20px
  body-sm:
    fontFamily: Inter
    fontSize: 12px
    fontWeight: '400'
    lineHeight: 16px
  label-md:
    fontFamily: JetBrains Mono
    fontSize: 12px
    fontWeight: '500'
    lineHeight: 16px
    letterSpacing: 0.02em
  label-xs:
    fontFamily: JetBrains Mono
    fontSize: 10px
    fontWeight: '500'
    lineHeight: 12px
  code-block:
    fontFamily: JetBrains Mono
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  base: 4px
  xs: 4px
  sm: 8px
  md: 16px
  lg: 24px
  xl: 32px
  container-max: 1440px
  sidebar-width: 280px
---

## Brand & Style
The design system focuses on a **Technical / Professional** aesthetic tailored for hardware engineers and embedded developers. It bridges the gap between high-level web interfaces and low-level diagnostic tools like oscilloscopes. The goal is to maximize data density without sacrificing clarity, evoking a sense of precision, reliability, and real-time responsiveness.

The style is characterized by a "Workstation" feel: clean, structured, and utilitarian. It avoids decorative elements in favor of functional visual cues, using whitespace strategically to group complex signal data and debugging logs.

## Colors
The palette is rooted in functional hardware aesthetics.
- **Primary Red (#e7352c):** Reserved for critical actions, active states of hardware connection, and branding accents.
- **Surface Neutrals:** A range of cool grays (from `#f8f9fa` for backgrounds to `#ffffff` for cards) provides a non-distracting canvas for waveform visualizations.
- **Borders & Dividers:** Medium grays (`#dee2e6`) define the grid and separate high-density data columns.
- **Signal Colors:** Success green for "Buffer OK" and Warning amber for "Dropped Packets" or "Underflow" events.

## Typography
The system employs a dual-font strategy:
1. **JetBrains Mono:** Used for headlines, labels, and all numerical/technical data. Its monospaced nature ensures that bitrates, hex values, and memory addresses align perfectly for visual scanning.
2. **Inter:** Used for general UI text, descriptions, and tooltips to provide a modern, highly legible contrast to the technical data.

Scale is kept compact to allow more information on screen, with a focus on small, clear labels for "Meter" and "Graph" axis points.

## Layout & Spacing
The layout follows a **Fixed-Fluid Hybrid Grid**. A permanent left sidebar contains connection settings and device parameters, while the main dashboard area uses a responsive grid of "Monitor Modules."

- **Density:** Use an 8px base grid, but allow for 4px increments in dense data tables or waveform control panels.
- **Modules:** Content is organized into cards. On desktop, use a 12-column grid where waveform viewers span 8-12 columns and smaller telemetry tiles span 2-4 columns.
- **Breakpoints:** 
  - Desktop: 1200px+ (Full visibility)
  - Tablet: 768px (Sidebar collapses to icons)
  - Mobile: Under 768px (Stacked view, prioritize status over waveforms)

## Elevation & Depth
Depth is used subtly to distinguish the background from interactive control surfaces.
- **Level 0 (Background):** `#f8f9fa`.
- **Level 1 (Cards/Modules):** White surface with a `1px` border of `#dee2e6` and a very soft, high-diffusion shadow (0px 2px 8px rgba(0,0,0,0.05)).
- **Level 2 (Modals/Popovers):** Standard elevation with a sharper shadow to indicate temporary focus.
- **Inset Depth:** Used for waveform containers and real-time logs to simulate an "embedded" screen look, using a light interior border.

## Shapes
The shape language balances modern software trends with technical utility.
- **Cards:** Use `12px` to `16px` (rounded-lg/xl) to soften the information-dense layout.
- **Buttons & Inputs:** Use `6px` (rounded-sm) for a more tool-like, precise feel.
- **Badges:** Pill-shaped (`999px`) for status indicators (e.g., "Online", "Sampling").
- **Segmented Controls:** Enclosed in a single rounded container to clearly group related toggles (e.g., Mono/Stereo, 16-bit/32-bit).

## Components
- **Primary Button:** Solid `#e7352c` background, white text. High-contrast and immediately identifiable as the "Action" color.
- **Segmented Control:** A "toggle group" style with a light gray background and a white raised "pill" for the active state.
- **Waveform Viewer:** A specialized card component with a dark (charcoal) internal plot area. Use high-contrast stroke colors (Cyan or Lime) for the PCM data stream.
- **Compact Inputs:** Form fields should have a height of 32px, using `label-md` typography placed inline or directly above the field to save vertical space.
- **Data Tables:** Zebra-striping (light gray) with monospaced text. No vertical borders; use horizontal dividers only.
- **Telemetry Chips:** Small, pill-shaped badges showing real-time stats like `Freq: 44.1kHz` or `Buffer: 98%`.
- **Vertical Meters:** PCM peak meters using a gradient from green to red, indicating dBFS levels.