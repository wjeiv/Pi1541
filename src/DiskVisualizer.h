// Pi1541 - Disk Visualizer for Primary Screen
// Renders a graphical floppy disk with track/sector visualization
// and read head animation using the Pi1541's Screen drawing primitives.
//
// Usage:
//   1. Call RenderDisk() once when a disk is loaded (from DisplayDiskInfo)
//   2. Call UpdateHead() from UpdateScreen() when track or LED state changes
//      for real-time read head movement and sector access highlighting.

#ifndef DISK_VISUALIZER_H
#define DISK_VISUALIZER_H

#include "ScreenBase.h"
#include "DiskImage.h"

class DiskVisualizer {
public:
    DiskVisualizer();

    // Full render: draws the complete disk with BAM-based sector coloring.
    // Call once when a disk image is loaded.
    void RenderDisk(ScreenBase* screen, int x, int y, int width, int height,
                    DiskImage* diskImage);

    // Lightweight update: moves the read head and highlights the active sector.
    // Call from UpdateScreen() when halfTrack or LED state changes.
    // halfTrack: raw half-track value from Drive::Track() (0-69)
    // ledOn: true if drive LED is on (indicates active read/write)
    void UpdateHead(ScreenBase* screen, unsigned int halfTrack,
                    unsigned int headBitOffset, bool ledOn);

    // Check if a disk has been rendered (geometry is valid)
    bool IsInitialized() const { return m_initialized; }

    // Reset state (e.g. when disk is ejected)
    void Reset();

private:
    // Drawing helpers
    void FillCircle(ScreenBase* screen, int cx, int cy, int radius, u32 color);
    void RedrawHeadStrip(ScreenBase* screen, int highlightTrack,
                         int highlightSector, bool highlightActive);
    void DrawHeadAt(ScreenBase* screen, int halfTrack, bool active);
    void DrawTrackLabel(ScreenBase* screen, int halfTrack, bool active);
    void DrawTrackSectors(ScreenBase* screen, int trackIndex,
                          int highlightSector, bool highlightActive);
    u32 GetSectorColor(int track, int sector) const;
    int HeadYForHalfTrack(int halfTrack) const;

    // Stored geometry from RenderDisk
    int m_x, m_y, m_width, m_height;
    int m_cx, m_cy;
    int m_maxRadius;
    int m_dataInnerR, m_dataOuterR;
    int m_hubRadius, m_labelRadius;
    int m_trackCount;
    int m_trackWidth;
    int m_headWidth;

    // State tracking
    DiskImage* m_diskImage;
    int m_oldHalfTrack;
    bool m_oldLedOn;
    bool m_initialized;
    int m_lastHighlightTrack;
    int m_lastHighlightSector;
    bool m_lastHighlightActive;

    // BAM cache: true = sector is allocated (has data)
    bool m_sectorUsed[42][24];
    int m_bitsPerTrack[42];
    int m_sectorsPerTrack[42];

    // Color constants (RGBA format)
    static const u32 COLOR_EMPTY;
    static const u32 COLOR_DATA;
    static const u32 COLOR_ACTIVE;
    static const u32 COLOR_DIRECTORY;
    static const u32 COLOR_DISK_BG;
    static const u32 COLOR_DISK_LABEL;
    static const u32 COLOR_DISK_HUB;
    static const u32 COLOR_HEAD;
    static const u32 COLOR_HEAD_GLOW;
    static const u32 COLOR_HEAD_ARM;
    static const u32 COLOR_TRACK_LABEL;
    static const u32 COLOR_SECTOR_IDLE;
    static const u32 COLOR_SECTOR_GAP;
};

#endif // DISK_VISUALIZER_H
