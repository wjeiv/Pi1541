// Pi1541 - Disk Visualizer Implementation
// Uses ScreenBase drawing primitives to render floppy disk visualization.
// Integer-only math to avoid floating point linking issues on bare metal.
//
// RenderDisk: full render with BAM, called once when disk loaded.
// UpdateHead: lightweight head-only update, called from UpdateScreen loop.

#include "DiskVisualizer.h"
#include "types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Integer square root (no floating point)
static int int_sqrt(int x) {
    if (x <= 0) return 0;
    int r = 0;
    int bit = 1 << 30;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

// Integer atan2: returns angle in 0-255 range (0 = east, 64 = south, 128 = west, 192 = north)
static int int_atan2(int y, int x) {
    if (x == 0 && y == 0) return 0;
    if (x == 0) return (y > 0) ? 64 : 192;
    if (y == 0) return (x > 0) ? 0 : 128;

    int abs_y = y < 0 ? -y : y;
    int abs_x = x < 0 ? -x : x;

    int angle;
    if (abs_y < abs_x)
        angle = (abs_y * 32) / abs_x;
    else
        angle = 64 - ((abs_x * 32) / abs_y);

    if (x < 0) angle = 128 - angle;
    if (y < 0) angle = 256 - angle;

    return angle & 255;
}

// Color constants
const u32 DiskVisualizer::COLOR_EMPTY       = RGBA(10, 10, 10, 255);
const u32 DiskVisualizer::COLOR_DATA        = RGBA(40, 200, 40, 255);
const u32 DiskVisualizer::COLOR_ACTIVE      = RGBA(255, 0, 0, 255);
const u32 DiskVisualizer::COLOR_DIRECTORY   = RGBA(60, 180, 220, 255);
const u32 DiskVisualizer::COLOR_DISK_BG     = RGBA(50, 35, 20, 255);
const u32 DiskVisualizer::COLOR_DISK_LABEL  = RGBA(220, 210, 180, 255);
const u32 DiskVisualizer::COLOR_DISK_HUB    = RGBA(160, 160, 170, 255);
const u32 DiskVisualizer::COLOR_HEAD        = RGBA(220, 30, 30, 255);
const u32 DiskVisualizer::COLOR_HEAD_GLOW   = RGBA(255, 140, 30, 255);
const u32 DiskVisualizer::COLOR_HEAD_ARM    = RGBA(80, 80, 90, 255);
const u32 DiskVisualizer::COLOR_TRACK_LABEL = RGBA(255, 255, 255, 255);
const u32 DiskVisualizer::COLOR_SECTOR_IDLE = RGBA(100, 100, 100, 255);
const u32 DiskVisualizer::COLOR_SECTOR_GAP  = RGBA(35, 25, 12, 255);

DiskVisualizer::DiskVisualizer()
    : m_x(0), m_y(0), m_width(0), m_height(0)
    , m_cx(0), m_cy(0), m_maxRadius(0)
    , m_dataInnerR(0), m_dataOuterR(0)
    , m_hubRadius(0), m_labelRadius(0)
    , m_trackCount(0), m_trackWidth(0), m_headWidth(0)
    , m_diskImage(0), m_oldHalfTrack(-1), m_oldLedOn(false)
    , m_initialized(false)
    , m_lastHighlightTrack(-1), m_lastHighlightSector(-1)
    , m_lastHighlightActive(false)
{
    memset(m_sectorUsed, 0, sizeof(m_sectorUsed));
    memset(m_bitsPerTrack, 0, sizeof(m_bitsPerTrack));
    memset(m_sectorsPerTrack, 0, sizeof(m_sectorsPerTrack));
}

void DiskVisualizer::Reset()
{
    m_initialized = false;
    m_oldHalfTrack = -1;
    m_oldLedOn = false;
    m_diskImage = 0;
    m_lastHighlightTrack = -1;
    m_lastHighlightSector = -1;
    m_lastHighlightActive = false;
    memset(m_sectorUsed, 0, sizeof(m_sectorUsed));
    memset(m_bitsPerTrack, 0, sizeof(m_bitsPerTrack));
    memset(m_sectorsPerTrack, 0, sizeof(m_sectorsPerTrack));
}

void DiskVisualizer::FillCircle(ScreenBase* screen, int cx, int cy, int radius, u32 color)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = int_sqrt(radius * radius - dy * dy);
        screen->DrawRectangle(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

// Map a half-track to a Y position on screen (head is on south side)
int DiskVisualizer::HeadYForHalfTrack(int halfTrack) const
{
    if (m_trackCount <= 1) return m_cy + m_dataOuterR;
    // Full track index (0-based), with half-track interpolation
    // halfTrack 0 = outermost (track 1), higher = innermost
    int range = m_dataOuterR - m_dataInnerR;
    // Map halfTrack to a position: 0 = outer, (trackCount*2-2) = inner
    int maxHT = (m_trackCount - 1) * 2;
    if (maxHT <= 0) maxHT = 1;
    int offset = (halfTrack * range) / maxHT;
    return m_cy + m_dataOuterR - offset;
}

// Get color for a given track/sector based on BAM cache
u32 DiskVisualizer::GetSectorColor(int track, int sector) const
{
    if (track == 17) { // Directory track (track 18, 0-indexed = 17)
        return m_sectorUsed[track][sector] ? COLOR_DIRECTORY : COLOR_EMPTY;
    }
    return m_sectorUsed[track][sector] ? COLOR_DATA : COLOR_EMPTY;
}

// Redraw the narrow vertical strip where the head arm lives (erases old head)
// No blanking rectangle — each pixel is drawn directly to avoid flicker
void DiskVisualizer::RedrawHeadStrip(ScreenBase* screen, int highlightTrack,
                                     int highlightSector, bool highlightActive)
{
    int stripHalfW = m_headWidth + 4;
    int stripLeft = m_cx - stripHalfW - 1;
    int stripRight = m_cx + stripHalfW + 1;
    int stripTop = m_cy + m_dataInnerR - 4;
    int stripBottom = m_cy + m_dataOuterR + 6;

    for (int py = stripTop; py <= stripBottom; ++py) {
        int dy = py - m_cy;
        for (int px = stripLeft; px <= stripRight; ++px) {
            int dx = px - m_cx;
            int dist = int_sqrt(dx * dx + dy * dy);

            // Outside the data ring — draw disk background
            if (dist < m_dataInnerR || dist > m_dataOuterR) {
                screen->PlotPixel(px, py, COLOR_DISK_BG);
                continue;
            }

            int fromOuter = m_dataOuterR - dist;
            int t = fromOuter / m_trackWidth;
            if (t < 0 || t >= m_trackCount) {
                screen->PlotPixel(px, py, COLOR_DISK_BG);
                continue;
            }

            int trackTop = t * m_trackWidth;
            int trackBot = trackTop + m_trackWidth;
            if (fromOuter < trackTop + 1 || fromOuter > trackBot - 1) {
                screen->PlotPixel(px, py, COLOR_SECTOR_GAP);
                continue;
            }

            int spt = m_sectorsPerTrack[t] > 0 ? m_sectorsPerTrack[t] : DiskImage::SectorsPerTrackD64(t);
            if (spt <= 0) spt = 17;

            int angle = int_atan2(dy, dx);
            int sectorAngle = 256 / spt;
            int sectorIndex = angle / sectorAngle;
            if (sectorIndex < 0) sectorIndex = 0;
            if (sectorIndex >= spt) sectorIndex = spt - 1;

            int sectorStart = sectorIndex * sectorAngle;
            int sectorEnd = sectorStart + sectorAngle;
            int gap = sectorAngle / 10;
            if (angle < sectorStart + gap || angle > sectorEnd - gap) {
                screen->PlotPixel(px, py, COLOR_SECTOR_GAP);
                continue;
            }

            u32 baseColor = GetSectorColor(t, sectorIndex);
            if (!baseColor) baseColor = COLOR_SECTOR_IDLE;

            if (highlightActive && t == highlightTrack) {
                if (highlightSector >= 0) {
                    if (sectorIndex == highlightSector)
                        screen->PlotPixel(px, py, COLOR_ACTIVE);
                    else
                        screen->PlotPixel(px, py, baseColor);
                } else {
                    if (baseColor != COLOR_EMPTY)
                        screen->PlotPixel(px, py, COLOR_ACTIVE);
                    else
                        screen->PlotPixel(px, py, baseColor);
                }
            } else {
                screen->PlotPixel(px, py, baseColor);
            }
        }
    }
}

// Draw the read head carriage, arm, and track number inside the head
void DiskVisualizer::DrawHeadAt(ScreenBase* screen, int halfTrack, bool active)
{
    int headY = HeadYForHalfTrack(halfTrack);
    int headX = m_cx;
    int headTop = m_cy + m_dataInnerR - 2;
    int headBottom = m_cy + m_dataOuterR + 4;

    // Head carriage sized to fit 2-digit track number (8x8 font)
    int carriageHalfH = 5;  // 11px tall total, enough for 8px font + padding
    int carriageHalfW = Max((u32)m_headWidth, (u32)9); // at least 18px wide for 2 chars

    // Head arm (vertical rail)
    screen->DrawRectangle(headX - 1, headTop, headX + 1, headBottom, COLOR_HEAD_ARM);

    // Glow when active (draw BEHIND carriage)
    if (active) {
        screen->DrawRectangle(headX - carriageHalfW - 2, headY - carriageHalfH - 2,
                              headX + carriageHalfW + 2, headY + carriageHalfH + 2,
                              COLOR_HEAD_GLOW);
    }

    // Head carriage
    screen->DrawRectangle(headX - carriageHalfW, headY - carriageHalfH,
                          headX + carriageHalfW, headY + carriageHalfH,
                          COLOR_HEAD);

    // Track number inside the head carriage
    char buf[4];
    int fullTrack = (halfTrack >> 1) + 1;
    snprintf(buf, sizeof(buf), "%d", fullTrack);

    // Center the text inside the carriage
    int textW = (fullTrack >= 10) ? 16 : 8; // 1 or 2 chars * 8px
    int textX = headX - textW / 2;
    int textY = headY - 4; // vertically center 8px font in carriage

    screen->PrintText(false, textX, textY, buf, COLOR_TRACK_LABEL, COLOR_HEAD);
}

// DrawTrackLabel is no longer used (track number is now inside the head)
void DiskVisualizer::DrawTrackLabel(ScreenBase* screen, int halfTrack, bool active)
{
    (void)screen; (void)halfTrack; (void)active;
}

void DiskVisualizer::DrawTrackSectors(ScreenBase* screen, int trackIndex,
                                      int highlightSector, bool highlightActive)
{
    int outerR = m_dataOuterR - trackIndex * m_trackWidth;
    int innerR = outerR - m_trackWidth;
    if (outerR <= innerR) return;

    int dyStart = -outerR;
    int dyEnd = outerR;

    int spt = m_sectorsPerTrack[trackIndex];
    if (spt <= 0) spt = DiskImage::SectorsPerTrackD64(trackIndex);
    if (spt <= 0) spt = 17;

    int sectorAngle = 256 / spt;
    int gap = sectorAngle / 10;

    for (int dy = dyStart; dy <= dyEnd; dy++) {
        int py = m_cy + dy;
        int outerDx = int_sqrt(outerR * outerR - dy * dy);
        int innerDx = 0;
        if (abs(dy) < innerR)
            innerDx = int_sqrt(innerR * innerR - dy * dy);

        for (int dx = -outerDx; dx <= outerDx; dx++) {
            if (abs(dx) < innerDx)
                continue;

            int px = m_cx + dx;
            int dist = int_sqrt(dx * dx + dy * dy);
            if (dist < innerR + 1 || dist > outerR - 1)
                continue;

            int angle = int_atan2(dy, dx);
            int sectorIndex = angle / sectorAngle;
            if (sectorIndex < 0) sectorIndex = 0;
            if (sectorIndex >= spt) sectorIndex = spt - 1;

            int sectorStart = sectorIndex * sectorAngle;
            int sectorEnd = sectorStart + sectorAngle;
            if (angle < sectorStart + gap || angle > sectorEnd - gap)
            {
                screen->PlotPixel(px, py, COLOR_SECTOR_GAP);
                continue;
            }

            u32 baseColor = GetSectorColor(trackIndex, sectorIndex);
            if (!baseColor) baseColor = COLOR_SECTOR_IDLE;

            if (highlightActive) {
                if (highlightSector >= 0) {
                    if (sectorIndex == highlightSector)
                        screen->PlotPixel(px, py, COLOR_ACTIVE);
                    else
                        screen->PlotPixel(px, py, baseColor);
                } else {
                    if (baseColor != COLOR_EMPTY)
                        screen->PlotPixel(px, py, COLOR_ACTIVE);
                    else
                        screen->PlotPixel(px, py, baseColor);
                }
            } else {
                screen->PlotPixel(px, py, baseColor);
            }
        }
    }
}

// ---- Full disk render (called once from DisplayDiskInfo) ----
void DiskVisualizer::RenderDisk(ScreenBase* screen, int x, int y, int width, int height,
                                DiskImage* diskImage)
{
    if (!diskImage || width <= 0 || height <= 0) return;

    // Store geometry
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
    m_diskImage = diskImage;

    m_trackCount = diskImage->LastTrackUsed();
    if (m_trackCount <= 0) m_trackCount = 35;
    // LastTrackUsed returns half-track count; convert to full tracks
    m_trackCount = m_trackCount >> 1;
    if (m_trackCount <= 0) m_trackCount = 35;
    if (m_trackCount > 42) m_trackCount = 42;

    m_cx = x + width / 2;
    m_cy = y + height / 2 - screen->ScaleY(12);
    int availableRadius = Min((u32)width, (u32)height) / 2;
    m_maxRadius = availableRadius - screen->ScaleY(40);
    if (m_maxRadius < 20) return;

    m_hubRadius = m_maxRadius / 8;
    m_labelRadius = m_maxRadius / 4;
    m_dataInnerR = m_labelRadius + 4;
    m_dataOuterR = m_maxRadius - 4;
    m_trackWidth = (m_dataOuterR - m_dataInnerR) / m_trackCount;
    if (m_trackWidth < 1) m_trackWidth = 1;
    m_headWidth = Max((u32)4, (u32)((m_dataOuterR - m_dataInnerR) / 30));

    // Read BAM from disk image
    memset(m_sectorUsed, 0, sizeof(m_sectorUsed));
    unsigned char bamBuffer[260];
    if (diskImage->GetDecodedSector(18, 0, bamBuffer)) {
        int lastTrackUsed = m_trackCount;

        // Guess 40-track format
        int guess40 = 0;
        if (lastTrackUsed >= 39) {
            int dolphin_sum = 0, speeddos_sum = 0;
            for (int i = 0; i < 20; i++) {
                dolphin_sum += bamBuffer[0xac + i];
                speeddos_sum += bamBuffer[0xc0 + i];
            }
            if (dolphin_sum == 0 && speeddos_sum != 0) guess40 = 0xc0;
            if (dolphin_sum != 0 && speeddos_sum == 0) guess40 = 0xac;
        }

        for (int bamTrack = 0; bamTrack < lastTrackUsed && bamTrack < 42; ++bamTrack) {
            int bamOffset;
            if (bamTrack >= 35)
                bamOffset = guess40;
            else
                bamOffset = BAM_OFFSET;

            if (!bamOffset) continue;

            int spt = DiskImage::SectorsPerTrackD64(bamTrack);
            for (int bit = 0; bit < spt && bit < 24; bit++) {
                u32 bits = bamBuffer[bamOffset + 1 + (bit >> 3) + bamTrack * BAM_ENTRY_SIZE];
                bool isFree = (bits & (1 << (bit & 0x7))) != 0;
                m_sectorUsed[bamTrack][bit] = !isFree; // true = allocated/has data
            }
        }
    }

    // 1. Draw disk surface
    FillCircle(screen, m_cx, m_cy, m_maxRadius, COLOR_DISK_BG);

    // 2. Draw track/sector data
    for (int t = 0; t < m_trackCount; ++t)
    {
        if (t < 42)
        {
            m_bitsPerTrack[t] = diskImage->BitsInTrack(t << 1);
            m_sectorsPerTrack[t] = DiskImage::SectorsPerTrackD64(t);
        }
        DrawTrackSectors(screen, t, -1, false);
    }

    // 3. Draw center label
    FillCircle(screen, m_cx, m_cy, m_labelRadius, COLOR_DISK_LABEL);

    // 4. Draw hub
    FillCircle(screen, m_cx, m_cy, m_hubRadius, COLOR_DISK_HUB);

    // 5. Draw hub hole
    FillCircle(screen, m_cx, m_cy, m_hubRadius / 2, RGBA(20, 20, 25, 255));

    m_oldHalfTrack = -1;
    m_oldLedOn = false;
    m_initialized = true;
    m_lastHighlightTrack = -1;
    m_lastHighlightSector = -1;
    m_lastHighlightActive = false;

    // 6. Draw initial head at track 1 (halfTrack 0)
    DrawHeadAt(screen, 0, false);
    m_oldHalfTrack = 0;
}

// ---- Lightweight head update (called from UpdateScreen) ----
void DiskVisualizer::UpdateHead(ScreenBase* screen, unsigned int halfTrack,
                                unsigned int headBitOffset, bool ledOn)
{
    if (!m_initialized || !screen) return;

    int trackIndex = (int)(halfTrack >> 1);
    if (trackIndex < 0) trackIndex = 0;
    if (trackIndex >= m_trackCount) trackIndex = m_trackCount - 1;

    int bitsInTrack = (trackIndex >= 0 && trackIndex < 42) ? m_bitsPerTrack[trackIndex] : 0;
    if (!bitsInTrack && m_diskImage)
        bitsInTrack = m_diskImage->BitsInTrack(trackIndex << 1);

    int sectorIndex = -1;
    if (bitsInTrack > 0)
    {
        int spt = m_sectorsPerTrack[trackIndex];
        if (spt <= 0) spt = DiskImage::SectorsPerTrackD64(trackIndex);
        if (spt > 0)
        {
            // Calculate sector based on bit position within track
            // Each sector occupies bitsInTrack/spt bits
            int bitsPerSector = bitsInTrack / spt;
            sectorIndex = (headBitOffset % bitsInTrack) / bitsPerSector;
            if (sectorIndex >= spt) sectorIndex = spt - 1;
        }
    }

    bool highlightActive = ledOn;

    bool needsRedraw = (int)halfTrack != m_oldHalfTrack || ledOn != m_oldLedOn ||
                       trackIndex != m_lastHighlightTrack ||
                       sectorIndex != m_lastHighlightSector ||
                       highlightActive != m_lastHighlightActive;

    if (!needsRedraw)
        return;

    // If highlighted track/sector changed, redraw full track rings
    if (m_lastHighlightTrack >= 0 && m_lastHighlightTrack < m_trackCount &&
        (m_lastHighlightTrack != trackIndex ||
         m_lastHighlightSector != sectorIndex ||
         m_lastHighlightActive != highlightActive))
    {
        // Restore old track to normal (no highlight)
        DrawTrackSectors(screen, m_lastHighlightTrack, -1, false);
    }

    // Draw new highlighted track with active sector
    if (trackIndex >= 0 && trackIndex < m_trackCount)
        DrawTrackSectors(screen, trackIndex, sectorIndex, highlightActive);

    // Erase old head by redrawing the strip of disk under it
    RedrawHeadStrip(screen, trackIndex, sectorIndex, highlightActive);

    // Draw new head (includes track number inside carriage)
    DrawHeadAt(screen, halfTrack, ledOn);

    m_oldHalfTrack = halfTrack;
    m_oldLedOn = ledOn;
    m_lastHighlightTrack = trackIndex;
    m_lastHighlightSector = sectorIndex;
    m_lastHighlightActive = highlightActive;
}
