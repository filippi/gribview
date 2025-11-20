#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <limits>
#include <iomanip>
#include <cstring> // for strcmp
#include <cctype>  // for isspace
#include <cstdarg>
#include <filesystem>
#include <sstream>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <GL/glew.h>
#include <eccodes.h>

extern "C"
{
#include "tinyfiledialogs.h"
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Your colormap data/structures:
#include "colormap512.h"

// ----------------------------------------------------------
// Structure to hold one GRIB message
// ----------------------------------------------------------
struct GribMessage
{
    int index; // 1-based load order
    long level;
    std::string shortName;
    long dataTime;
    long dataDate;
    long Ni;
    long Nj;
    double lat1, lat2, lon1, lon2;
    double minVal, maxVal;
    std::string parameterUnits;
    std::string parameterName;

    // Instead of keeping the full handle we now also store:
    // the file path and the file offset at which this message starts.
    std::string filePath;
    long fileOffset;

    // The eccodes handle for on-demand (reopened later if needed):
    codes_handle *message;

    // Minimal key/value pairs loaded at startup:
    std::map<std::string, std::string> keyValueMap;

    // Indicates whether we have fully loaded *all* keys for "More Info"
    bool fullyPopulated;

    // New: selection flag for multi-select
    bool selected;

    GribMessage() : message(nullptr), fullyPopulated(false), selected(false) {}
};

// ----------------------------------------------------------
// Global data
// ----------------------------------------------------------
static SDL_Window *g_Window = nullptr;
static SDL_GLContext g_GLContext = nullptr;
static int g_WindowWidth = 1280;
static int g_WindowHeight = 800;

static std::vector<GribMessage> g_GribMessages;
// Active (first selected) index:
static int g_SelectedMessageIndex = -1;
static bool g_AutoFit = true;
static float g_UserMinVal = 0.f;
static float g_UserMaxVal = 1.f;

// Colormap
static std::string g_ChosenColormapName = "jet";
static GLuint g_TextureID = 0;
static int g_TexWidth = 0;
static int g_TexHeight = 0;

// Pan & zoom
static float g_Zoom = 1.0f;
static float g_OffsetX = 0.0f;
static float g_OffsetY = 0.0f;
static bool g_IsPanning = false;
static ImVec2 g_PanStart;

// Save‐image path
static char g_SaveImagePath[512] = "coloured.png";

// “Inspector” popup for “More Info”
static bool g_ShowInspector = false;
static int g_InspectorIndex = -1; // which message are we inspecting?
static char g_SaveGribPath[512] = "selection.grib";
static char g_SaveSinglePath[512] = "message.grib";
static std::string g_SaveSelectionStatus;
static bool g_SaveSelectionSuccess = false;
static int g_LastSelectionAnchor = -1;
static int g_ScrollPendingIndex = -1;
static bool g_RequestFileDialog = false;
static std::string g_LastOpenedDir;
static bool g_AddMarkerMode = false;
static int g_DraggingMarkerIndex = -1;
static std::string g_ExtractionStatus;
static char g_MarkersCsvPath[512] = "markers.csv";
static bool g_ExtractionRunning = false;
static size_t g_ExtractionNextIndex = 0;
static int g_PlotClickRequest = -1;
static int g_PlotClickedIndex = -1;

struct MarkerSample
{
    std::string messageKey;
    double value;
};

struct Marker
{
    int id;
    double lat;
    double lon;
    std::vector<MarkerSample> series;
    char csvPath[512];
    std::string status;
};

static std::vector<Marker> g_Markers;

// For the GRIB table and keys select popup
struct UiState
{
    // “Displayed keys” are the columns that will be shown.
    std::vector<std::string> displayedKeys;
    // “Available keys” is built from the first message (filtered).
    std::vector<std::string> availableKeys;
    std::string sortKey;
    bool sortAscending;
} g_UiState;

static void LoadGribFileAppend(const std::string &path);
static void ConfigureEcCodesEnvironment();
static codes_handle *ReopenGribMessage(GribMessage &gm);
static void GetMessageValuesAndRange(codes_handle *h,
                                     std::vector<double> &outValues,
                                     double &outMinVal,
                                     double &outMaxVal);
static void ClearAllSelections();
static void RefreshSelectionState(bool requestScroll, int preferredIndex);
static void UpdateWindowTitle();
static void EnsureParameterInfo(GribMessage &gm);

// ----------------------------------------------------------
// Destroy texture helper
// ----------------------------------------------------------
static void DestroyTexture(GLuint &texID)
{
    if (texID)
    {
        glDeleteTextures(1, &texID);
        texID = 0;
    }
}

// ----------------------------------------------------------
// Create texture from RGBA data
// ----------------------------------------------------------
static GLuint CreateTextureFromData(const unsigned char *data, int width, int height)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ----------------------------------------------------------
// Save-path helpers
// ----------------------------------------------------------
static void SetPathBuffer(char *dst, size_t dstSize, const std::string &value)
{
    if (dstSize == 0)
        return;
    snprintf(dst, dstSize, "%s", value.c_str());
    dst[dstSize - 1] = '\0';
}

static std::string GetHomeDirectory()
{
#if defined(_WIN32)
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (home && *home)
        return std::string(home);
    return std::filesystem::current_path().string();
}

static void UpdateSavePathsForDir(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    fs::path base = dir.empty() ? fs::path(GetHomeDirectory()) : dir;
    g_LastOpenedDir = base.string();
    SetPathBuffer(g_SaveImagePath, IM_ARRAYSIZE(g_SaveImagePath), (base / "coloured.png").string());
    SetPathBuffer(g_SaveGribPath, IM_ARRAYSIZE(g_SaveGribPath), (base / "selection.grib").string());
    SetPathBuffer(g_SaveSinglePath, IM_ARRAYSIZE(g_SaveSinglePath), (base / "message.grib").string());
    SetPathBuffer(g_MarkersCsvPath, IM_ARRAYSIZE(g_MarkersCsvPath), (base / "markers.csv").string());
}

static void EnsureParameterInfo(GribMessage &gm)
{
    if (!gm.parameterName.empty() && !gm.parameterUnits.empty())
        return;
    if (!gm.message)
        gm.message = ReopenGribMessage(gm);
    if (!gm.message)
        return;
    if (gm.parameterName.empty())
    {
        char buf[256];
        size_t len = sizeof(buf);
        if (codes_get_string(gm.message, "parameterName", buf, &len) == 0)
            gm.parameterName = buf;
    }
    if (gm.parameterUnits.empty())
    {
        char buf[128];
        size_t len = sizeof(buf);
        if (codes_get_string(gm.message, "parameterUnits", buf, &len) == 0)
            gm.parameterUnits = buf;
    }
}

static void UpdateWindowTitle()
{
    if (!g_Window)
        return;
    std::string title = "gribview";
    if (!g_GribMessages.empty())
    {
        std::string firstPath = g_GribMessages.front().filePath;
        std::filesystem::path p(firstPath);
        std::string name = p.filename().string();
        bool multiple = false;
        for (const auto &gm : g_GribMessages)
        {
            if (gm.filePath != firstPath)
            {
                multiple = true;
                break;
            }
        }
        title += " - " + name;
        if (multiple)
            title += " ...";
    }
    SDL_SetWindowTitle(g_Window, title.c_str());
}

static std::string BuildMessageKey(const GribMessage &gm)
{
    return gm.filePath + "|" + std::to_string(gm.fileOffset);
}

static GribMessage *FindMessageByKey(const std::string &key)
{
    for (auto &gm : g_GribMessages)
    {
        if (BuildMessageKey(gm) == key)
            return &gm;
    }
    return nullptr;
}

static ImU32 MarkerColor(int idx)
{
    static ImU32 palette[] = {
        IM_COL32(230, 76, 60, 255),   // red
        IM_COL32(52, 152, 219, 255),  // blue
        IM_COL32(46, 204, 113, 255),  // green
        IM_COL32(241, 196, 15, 255),  // yellow
        IM_COL32(155, 89, 182, 255),  // purple
        IM_COL32(230, 126, 34, 255),  // orange
        IM_COL32(22, 160, 133, 255),  // teal
        IM_COL32(127, 140, 141, 255)  // gray
    };
    size_t paletteSize = sizeof(palette) / sizeof(ImU32);
    if (paletteSize == 0)
        return IM_COL32(255, 255, 255, 255);
    return palette[idx % paletteSize];
}

static ImVec4 MarkerColorVec4(int idx)
{
    ImU32 c = MarkerColor(idx);
    return ImGui::ColorConvertU32ToFloat4(c);
}

static bool LatLonToGrid(const GribMessage &gm, double lat, double lon, double &fi, double &fj, int &i, int &j)
{
    if (gm.Ni <= 1 || gm.Nj <= 1)
        return false;
    double latRange = fabs(gm.lat1 - gm.lat2);
    if (latRange < 1e-9)
        return false;
    double lonRange = gm.lon2 - gm.lon1;
    if (lonRange < 0)
        lonRange += 360.0;
    if (lonRange <= 0)
        lonRange = 360.0;
    bool desc = (gm.lat1 > gm.lat2);
    fj = desc ? (gm.lat1 - lat) / latRange : (lat - gm.lat1) / latRange;
    fj = std::clamp(fj, 0.0, 1.0);
    double dlon = lon - gm.lon1;
    while (dlon < 0.0)
        dlon += 360.0;
    while (dlon > lonRange && lonRange < 360.0)
        dlon -= 360.0;
    fi = lonRange > 1e-9 ? (dlon / lonRange) : 0.0;
    fi = std::clamp(fi, 0.0, 1.0);
    i = (int)std::round(fi * (double)(gm.Ni - 1));
    j = (int)std::round(fj * (double)(gm.Nj - 1));
    if (i < 0)
        i = 0;
    if (j < 0)
        j = 0;
    if (i >= gm.Ni)
        i = (int)gm.Ni - 1;
    if (j >= gm.Nj)
        j = (int)gm.Nj - 1;
    return true;
}

static bool LatLonToScreen(const GribMessage &gm, double lat, double lon, float contentX, float contentY, ImVec2 &outPos)
{
    double fi, fj;
    int ii, jj;
    if (!LatLonToGrid(gm, lat, lon, fi, fj, ii, jj))
        return false;
    float px = contentX + g_OffsetX + (float)fi * (float)(gm.Ni - 1) * g_Zoom;
    float py = contentY + g_OffsetY + (float)fj * (float)(gm.Nj - 1) * g_Zoom;
    outPos = ImVec2(px, py);
    return true;
}

static bool SampleValueFromData(const GribMessage &gm, const std::vector<double> &data, double lat, double lon, double &outVal)
{
    double fi, fj;
    int ii, jj;
    if (!LatLonToGrid(gm, lat, lon, fi, fj, ii, jj))
        return false;
    size_t idx = (size_t)jj * (size_t)gm.Ni + (size_t)ii;
    if (idx >= data.size())
        return false;
    outVal = data[idx];
    return true;
}

static bool LoadMessageData(GribMessage &gm, std::vector<double> &outData)
{
    if (!gm.message)
        gm.message = ReopenGribMessage(gm);
    if (!gm.message)
        return false;
    double minVal, maxVal;
    GetMessageValuesAndRange(gm.message, outData, minVal, maxVal);
    return !outData.empty();
}

static void ClearMarkerSeries()
{
    for (auto &m : g_Markers)
    {
        m.series.clear();
        m.status.clear();
    }
    g_PlotClickedIndex = -1;
}

static void StartMarkerExtraction()
{
    if (g_Markers.empty() || g_GribMessages.empty())
        return;
    ClearMarkerSeries();
    g_PlotClickedIndex = -1;
    g_ExtractionStatus = "Extracting...";
    size_t startIdx = (g_SelectedMessageIndex >= 0 && g_SelectedMessageIndex < (int)g_GribMessages.size())
                          ? (size_t)g_SelectedMessageIndex
                          : 0;
    g_ExtractionNextIndex = startIdx;
    g_ExtractionRunning = true;
}

static void StepMarkerExtraction()
{
    if (!g_ExtractionRunning)
        return;
    if (g_Markers.empty() || g_GribMessages.empty())
    {
        g_ExtractionRunning = false;
        g_ExtractionStatus = "No markers or messages";
        return;
    }
    if (g_ExtractionNextIndex >= g_GribMessages.size())
    {
        g_ExtractionRunning = false;
        g_ExtractionStatus = "Extraction done";
        return;
    }
    size_t idx = g_ExtractionNextIndex++;
    GribMessage &gm = g_GribMessages[idx];
    std::vector<double> data;
    bool hasData = LoadMessageData(gm, data);
    for (auto &m : g_Markers)
    {
        MarkerSample samp;
        samp.messageKey = BuildMessageKey(gm);
        samp.value = std::numeric_limits<double>::quiet_NaN();
        if (hasData)
        {
            double v = 0.0;
            if (SampleValueFromData(gm, data, m.lat, m.lon, v))
                samp.value = v;
        }
        m.series.push_back(samp);
    }
    std::vector<double>().swap(data);
    ClearAllSelections();
    gm.selected = true;
    g_LastSelectionAnchor = (int)idx;
    RefreshSelectionState(true, (int)idx);
    g_ExtractionStatus = "Frame " + std::to_string(idx + 1) + "/" + std::to_string(g_GribMessages.size());
}

static bool SaveMarkersCsv()
{
    if (g_Markers.empty() || g_GribMessages.empty())
        return false;
    FILE *f = fopen(g_MarkersCsvPath, "w");
    if (!f)
        return false;
    fprintf(f, "markerID");
    for (const auto &col : g_UiState.displayedKeys)
        fprintf(f, ",%s", col.c_str());
    fprintf(f, ",lat,lon,value\n");
    for (size_t mi = 0; mi < g_Markers.size(); mi++)
    {
        const Marker &m = g_Markers[mi];
        for (size_t row = 0; row < m.series.size(); row++)
        {
            const MarkerSample &s = m.series[row];
            GribMessage *gm = FindMessageByKey(s.messageKey);
            fprintf(f, "%zu", mi + 1);
            for (const auto &col : g_UiState.displayedKeys)
            {
                fprintf(f, ",");
                if (gm)
                {
                    auto it = gm->keyValueMap.find(col);
                    if (it != gm->keyValueMap.end())
                        fprintf(f, "%s", it->second.c_str());
                }
            }
            fprintf(f, ",%.6f,%.6f", m.lat, m.lon);
            if (std::isnan(s.value))
                fprintf(f, ",");
            else
                fprintf(f, ",%.10g", s.value);
            fprintf(f, "\n");
        }
    }
    fclose(f);
    return true;
}

static void CreateMarkerAt(double lat, double lon)
{
    Marker m;
    m.id = (int)g_Markers.size() + 1;
    m.lat = lat;
    m.lon = lon;
    g_Markers.push_back(m);
}

static void RenumberMarkers()
{
    for (size_t i = 0; i < g_Markers.size(); ++i)
        g_Markers[i].id = (int)i + 1;
}

static void RemoveMarkerAt(size_t idx)
{
    if (idx >= g_Markers.size())
        return;
    g_Markers.erase(g_Markers.begin() + idx);
    RenumberMarkers();
    ClearMarkerSeries();
    g_ExtractionRunning = false;
    g_ExtractionStatus = "Marker removed; cleared series";
}

static void DrawMarkersPlot(const ImVec2 &size)
{
    if (g_Markers.empty())
    {
        ImGui::Text("No marker data");
        return;
    }
    g_PlotClickRequest = -1;
    float width = size.x;
    float height = size.y;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 plotMin = origin;
    ImVec2 plotMax(origin.x + width, origin.y + height);
    dl->AddRectFilled(plotMin, plotMax, IM_COL32(30, 30, 30, 255));
    dl->AddRect(plotMin, plotMax, IM_COL32(80, 80, 80, 255));
    size_t maxCount = 0;
    double vMin = std::numeric_limits<double>::infinity();
    double vMax = -std::numeric_limits<double>::infinity();
    for (const auto &m : g_Markers)
    {
        if (m.series.size() > maxCount)
            maxCount = m.series.size();
        for (const auto &s : m.series)
        {
            if (std::isnan(s.value))
                continue;
            vMin = std::min(vMin, s.value);
            vMax = std::max(vMax, s.value);
        }
    }
    if (maxCount < 2)
        maxCount = 2;
    if (!std::isfinite(vMin) || !std::isfinite(vMax) || fabs(vMax - vMin) < 1e-12)
    {
        vMin = -1.0;
        vMax = 1.0;
    }
    double pad = (vMax - vMin) * 0.05;
    vMin -= pad;
    vMax += pad;
    auto toScreen = [&](size_t idx, double value) -> ImVec2 {
        float t = (float)idx / (float)(maxCount - 1);
        float x = plotMin.x + t * (width - 1.0f);
        float factor = (float)((value - vMin) / (vMax - vMin));
        float y = plotMax.y - factor * (height - 1.0f);
        return ImVec2(x, y);
    };
    // Axes and ticks
    dl->AddLine(ImVec2(plotMin.x, plotMax.y), ImVec2(plotMax.x, plotMax.y), IM_COL32(200, 200, 200, 255));
    dl->AddLine(ImVec2(plotMin.x, plotMin.y), ImVec2(plotMin.x, plotMax.y), IM_COL32(200, 200, 200, 255));
    auto drawTick = [&](double value, bool major) {
        float factor = (float)((value - vMin) / (vMax - vMin));
        float y = plotMax.y - factor * (height - 1.0f);
        ImU32 col = major ? IM_COL32(200, 200, 200, 255) : IM_COL32(120, 120, 120, 120);
        dl->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), col, major ? 1.5f : 1.0f);
        dl->AddText(ImVec2(plotMin.x + 4, y - 10), IM_COL32(230, 230, 230, 255), (std::to_string((float)value)).c_str());
    };
    drawTick(vMax, true);
    drawTick(vMin, true);
    double mid = 0.5 * (vMin + vMax);
    drawTick(mid, false);
    if (vMin < 0.0 && vMax > 0.0)
        drawTick(0.0, true);
    // Data lines
    for (size_t mi = 0; mi < g_Markers.size(); mi++)
    {
        const Marker &m = g_Markers[mi];
        ImU32 col = MarkerColor((int)mi);
        ImVec2 prev;
        bool hasPrev = false;
        for (size_t k = 0; k < m.series.size(); k++)
        {
            const MarkerSample &s = m.series[k];
            if (std::isnan(s.value))
            {
                hasPrev = false;
                continue;
            }
            ImVec2 p = toScreen(k, s.value);
            if (hasPrev)
                dl->AddLine(prev, p, col, 2.0f);
            prev = p;
            hasPrev = true;
        }
    }
    if (g_PlotClickedIndex >= 0 && maxCount > 1)
    {
        float t = (float)g_PlotClickedIndex / (float)(maxCount - 1);
        float x = plotMin.x + t * (width - 1.0f);
        dl->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), IM_COL32(120, 220, 120, 200), 2.0f);
    }
    ImGui::Dummy(ImVec2(width, height));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImVec2 mp = ImGui::GetMousePos();
        float relX = (mp.x - plotMin.x) / (width - 1.0f);
        relX = std::clamp(relX, 0.0f, 1.0f);
        size_t idx = (size_t)std::round(relX * (float)(maxCount - 1));
        if (!g_GribMessages.empty())
        {
            g_PlotClickedIndex = (int)idx;
            g_PlotClickRequest = (int)std::min(idx, g_GribMessages.size() - 1);
        }
    }
}

// ----------------------------------------------------------
// Parse string to double (C++11 approach). Return true if ok.
// ----------------------------------------------------------
static bool parseDouble(const std::string &s, double &outValue)
{
    char *endPtr = nullptr;
    const char *cstr = s.c_str();
    outValue = strtod(cstr, &endPtr);
    if (endPtr == cstr)
        return false;
    while (endPtr && *endPtr)
    {
        if (!isspace((unsigned char)*endPtr))
            return false;
        endPtr++;
    }
    return true;
}

// ----------------------------------------------------------
// Compare function for sorting.
// Tries numeric compare; falls back to string compare.
// ----------------------------------------------------------
static bool compareGribMessages(const GribMessage &a,
                                const GribMessage &b,
                                const std::string &key,
                                bool ascending)
{
    auto ita = a.keyValueMap.find(key);
    auto itb = b.keyValueMap.find(key);
    std::string sa = (ita != a.keyValueMap.end()) ? ita->second : "";
    std::string sb = (itb != b.keyValueMap.end()) ? itb->second : "";
    double da, db;
    bool fa = parseDouble(sa, da);
    bool fb = parseDouble(sb, db);
    if (fa && fb)
        return ascending ? (da < db) : (da > db);
    return ascending ? (sa < sb) : (sa > sb);
}

// ----------------------------------------------------------
// On-demand retrieval of all keys for “More Info”
// (skips large arrays like "values", "bitmap")
// ----------------------------------------------------------
static void PopulateAllKeys(GribMessage &gm)
{
    if (!gm.message || gm.fullyPopulated)
        return;
    codes_keys_iterator *it =
        codes_keys_iterator_new(gm.message, GRIB_KEYS_ITERATOR_ALL_KEYS, NULL);
    while (codes_keys_iterator_next(it))
    {
        const char *keyName = codes_keys_iterator_get_name(it);
        if (!keyName)
            continue;
        if (!strcmp(keyName, "values") || !strcmp(keyName, "bitmap"))
            continue;
        if (gm.keyValueMap.find(keyName) == gm.keyValueMap.end())
        {
            char buf[1024];
            size_t bufLen = sizeof(buf);
            if (codes_get_string(gm.message, keyName, buf, &bufLen) == 0)
                gm.keyValueMap[keyName] = buf;
        }
    }
    codes_keys_iterator_delete(it);
    gm.fullyPopulated = true;
}

// ----------------------------------------------------------
// Unpack float data. Also compute min/max.
// ----------------------------------------------------------
static void GetMessageValuesAndRange(codes_handle *h,
                                     std::vector<double> &outValues,
                                     double &outMinVal,
                                     double &outMaxVal)
{
    outMinVal = std::numeric_limits<double>::infinity();
    outMaxVal = -std::numeric_limits<double>::infinity();
    size_t nvals = 0;
    if (codes_get_size(h, "values", &nvals) != 0 || nvals == 0)
    {
        outValues.clear();
        outMinVal = 0;
        outMaxVal = 0;
        return;
    }
    outValues.resize(nvals);
    codes_get_double_array(h, "values", outValues.data(), &nvals);
    for (size_t i = 0; i < nvals; i++)
    {
        double v = outValues[i];
        if (fabs(v - 9999.0) < 1e-8)
            v = std::numeric_limits<double>::quiet_NaN();
        outValues[i] = v;
        if (!std::isnan(v))
        {
            if (v < outMinVal)
                outMinVal = v;
            if (v > outMaxVal)
                outMaxVal = v;
        }
    }
    if (outMinVal == std::numeric_limits<double>::infinity())
        outMinVal = 0;
    if (outMaxVal == -std::numeric_limits<double>::infinity())
        outMaxVal = 0;
}

// ----------------------------------------------------------
// Reopen a GRIB message from file (if needed).
// ----------------------------------------------------------
static codes_handle *ReopenGribMessage(GribMessage &gm)
{
    FILE *f = fopen(gm.filePath.c_str(), "rb");
    if (!f)
        return nullptr;
    if (fseek(f, gm.fileOffset, SEEK_SET) != 0)
    {
        fclose(f);
        return nullptr;
    }
    int err = 0;
    codes_handle *h = codes_handle_new_from_file(nullptr, f, PRODUCT_GRIB, &err);
    fclose(f);
    return h;
}

// ----------------------------------------------------------
// Generate the display texture for the currently active message.
// Before using the message handle, re-open it if needed.
// ----------------------------------------------------------
static void GenerateTextureForSelectedMessage()
{
    DestroyTexture(g_TextureID);
    if (g_SelectedMessageIndex < 0 || g_SelectedMessageIndex >= (int)g_GribMessages.size())
        return;
    GribMessage &gm = g_GribMessages[g_SelectedMessageIndex];
    if (!gm.message)
        gm.message = ReopenGribMessage(gm);
    if (!gm.message)
        return;
    std::vector<double> data;
    double minVal, maxVal;
    GetMessageValuesAndRange(gm.message, data, minVal, maxVal);
    gm.minVal = minVal;
    gm.maxVal = maxVal;
    int width = (int)gm.Ni;
    int height = (int)gm.Nj;
    if (width <= 0 || height <= 0 || data.empty())
        return;
    if (g_AutoFit)
    {
        g_UserMinVal = (float)minVal;
        g_UserMaxVal = (float)maxVal;
    }
    g_TexWidth = width;
    g_TexHeight = height;
    auto it = colormapMap.find(g_ChosenColormapName);
    const ColorEntry *colorMap = (it != colormapMap.end()) ? it->second : greyColormap;
    int mapSize = colormapSize;
    double range = (double)g_UserMaxVal - (double)g_UserMinVal;
    std::vector<unsigned char> imageRGBA(width * height * 4, 0);
    for (int j = 0; j < height; j++)
    {
        for (int i = 0; i < width; i++)
        {
            int idx = j * width + i;
            double val = data[idx];
            int rgbaI = 4 * idx;
            if (std::isnan(val))
            {
                imageRGBA[rgbaI + 0] = 0;
                imageRGBA[rgbaI + 1] = 0;
                imageRGBA[rgbaI + 2] = 0;
                imageRGBA[rgbaI + 3] = 0;
            }
            else
            {
                double clamped = val;
                if (clamped < g_UserMinVal)
                    clamped = g_UserMinVal;
                if (clamped > g_UserMaxVal)
                    clamped = g_UserMaxVal;
                double t = (range > 1e-14) ? (clamped - g_UserMinVal) / range : 0.0;
                if (t < 0.0)
                    t = 0.0;
                if (t > 1.0)
                    t = 1.0;
                int cIdx = (int)(t * (mapSize - 1));
                if (cIdx < 0)
                    cIdx = 0;
                if (cIdx >= mapSize)
                    cIdx = mapSize - 1;
                const ColorEntry &c = colorMap[cIdx];
                imageRGBA[rgbaI + 0] = c[0];
                imageRGBA[rgbaI + 1] = c[1];
                imageRGBA[rgbaI + 2] = c[2];
                imageRGBA[rgbaI + 3] = 255;
            }
        }
    }
    g_TextureID = CreateTextureFromData(imageRGBA.data(), width, height);
}

// ----------------------------------------------------------
// Selection helpers
// ----------------------------------------------------------
static void ClearAllSelections()
{
    for (auto &msg : g_GribMessages)
        msg.selected = false;
}

static void SelectRangeInclusive(int startIdx, int endIdx)
{
    if (g_GribMessages.empty())
        return;
    int maxIndex = (int)g_GribMessages.size() - 1;
    startIdx = std::max(0, std::min(startIdx, maxIndex));
    endIdx = std::max(0, std::min(endIdx, maxIndex));
    if (startIdx > endIdx)
        std::swap(startIdx, endIdx);
    for (int i = 0; i <= maxIndex; ++i)
        g_GribMessages[i].selected = (i >= startIdx && i <= endIdx);
}

static void RefreshSelectionState(bool requestScroll, int preferredIndex = -1)
{
    int activeIndex = -1;
    if (preferredIndex >= 0 &&
        preferredIndex < (int)g_GribMessages.size() &&
        g_GribMessages[preferredIndex].selected)
    {
        activeIndex = preferredIndex;
    }
    else
    {
        for (size_t j = 0; j < g_GribMessages.size(); j++)
        {
            if (g_GribMessages[j].selected)
            {
                activeIndex = (int)j;
                break;
            }
        }
    }
    g_SelectedMessageIndex = activeIndex;
    if (g_SelectedMessageIndex >= 0)
    {
        if (requestScroll)
            g_ScrollPendingIndex = g_SelectedMessageIndex;
        GenerateTextureForSelectedMessage();
    }
    else
    {
        DestroyTexture(g_TextureID);
    }
}

static void ClearAllMessages()
{
    DestroyTexture(g_TextureID);
    for (auto &msg : g_GribMessages)
    {
        if (msg.message)
        {
            codes_handle_delete(msg.message);
            msg.message = nullptr;
        }
    }
    g_GribMessages.clear();
    g_SelectedMessageIndex = -1;
    g_LastSelectionAnchor = -1;
    g_ScrollPendingIndex = -1;
    g_SaveSelectionStatus.clear();
    g_SaveSelectionSuccess = false;
    ClearMarkerSeries();
    g_ExtractionRunning = false;
    g_ExtractionStatus = "";
    UpdateWindowTitle();
}

static void LoadFilesAndSelect(const std::vector<std::string> &paths)
{
    if (paths.empty())
        return;
    size_t previousCount = g_GribMessages.size();
    for (const auto &p : paths)
    {
        LoadGribFileAppend(p);
    }
    if (g_GribMessages.size() > previousCount)
    {
        ClearAllSelections();
        size_t selectIndex = previousCount;
        if (selectIndex >= g_GribMessages.size())
            selectIndex = g_GribMessages.size() - 1;
        g_GribMessages[selectIndex].selected = true;
        g_SelectedMessageIndex = (int)selectIndex;
        g_LastSelectionAnchor = (int)selectIndex;
        GenerateTextureForSelectedMessage();
    }
}

static void PromptFileDialogIfNeeded()
{
    if (!g_RequestFileDialog)
        return;
    g_RequestFileDialog = false;
    const char *patterns[] = {"*.grib", "*.grb", "*.grib2", "*.grb2", "*"};
    const char *choice = tinyfd_openFileDialog("Open GRIB files",
                                               "",
                                               5,
                                               patterns,
                                               "GRIB files",
                                               1);
    if (!choice)
        return;
    std::vector<std::string> files;
    std::string combined(choice);
    size_t start = 0;
    while (start < combined.size())
    {
        size_t sep = combined.find('|', start);
        std::string token = combined.substr(start, (sep == std::string::npos) ? std::string::npos : sep - start);
        if (!token.empty())
            files.push_back(token);
        if (sep == std::string::npos)
            break;
        start = sep + 1;
    }
    LoadFilesAndSelect(files);
}

// ----------------------------------------------------------
// Save current displayed image to PNG
// ----------------------------------------------------------
static void SaveCurrentImagePNG(const std::string &filename)
{
    if (g_SelectedMessageIndex < 0 || g_SelectedMessageIndex >= (int)g_GribMessages.size())
        return;
    const GribMessage &gm = g_GribMessages[g_SelectedMessageIndex];
    if (!gm.message)
        return;
    std::vector<double> data;
    double minVal, maxVal;
    GetMessageValuesAndRange(gm.message, data, minVal, maxVal);
    int width = (int)gm.Ni;
    int height = (int)gm.Nj;
    if (width <= 0 || height <= 0 || data.empty())
        return;
    auto it = colormapMap.find(g_ChosenColormapName);
    const ColorEntry *colorMap = (it != colormapMap.end()) ? it->second : greyColormap;
    int mapSize = colormapSize;
    double userMin = g_UserMinVal;
    double userMax = g_UserMaxVal;
    double range = userMax - userMin;
    std::vector<unsigned char> imageRGBA(width * height * 4, 0);
    for (int j = 0; j < height; j++)
    {
        for (int i = 0; i < width; i++)
        {
            int idx = j * width + i;
            double val = data[idx];
            int rgbaI = 4 * idx;
            if (std::isnan(val))
            {
                imageRGBA[rgbaI + 0] = 0;
                imageRGBA[rgbaI + 1] = 0;
                imageRGBA[rgbaI + 2] = 0;
                imageRGBA[rgbaI + 3] = 0;
            }
            else
            {
                double clamped = val;
                if (clamped < userMin)
                    clamped = userMin;
                if (clamped > userMax)
                    clamped = userMax;
                double t = (range > 1e-14) ? (clamped - userMin) / range : 0.0;
                if (t < 0.0)
                    t = 0.0;
                if (t > 1.0)
                    t = 1.0;
                int cIdx = (int)(t * (mapSize - 1));
                if (cIdx < 0)
                    cIdx = 0;
                if (cIdx >= mapSize)
                    cIdx = mapSize - 1;
                const ColorEntry &c = colorMap[cIdx];
                imageRGBA[rgbaI + 0] = c[0];
                imageRGBA[rgbaI + 1] = c[1];
                imageRGBA[rgbaI + 2] = c[2];
                imageRGBA[rgbaI + 3] = 255;
            }
        }
    }
    stbi_write_png(filename.c_str(), width, height, 4, imageRGBA.data(), width * 4);
}

// ----------------------------------------------------------
// Save a single GRIB message to file
// ----------------------------------------------------------
static bool SaveSingleMessageGrib(GribMessage &gm, const std::string &outPath)
{
    if (!gm.message)
        gm.message = ReopenGribMessage(gm);
    if (!gm.message)
        return false;
    FILE *out = fopen(outPath.c_str(), "wb");
    if (!out)
        return false;
    const void *buffer = nullptr;
    size_t size = 0;
    int err = codes_get_message(gm.message, &buffer, &size);
    bool ok = (!err && buffer && size > 0);
    if (ok)
    {
        fwrite(buffer, 1, size, out);
    }
    fclose(out);
    return ok;
}

// ----------------------------------------------------------
// Save multiple GRIB messages to one file
// ----------------------------------------------------------
static bool SaveMessagesToGrib(const std::vector<GribMessage *> &messages,
                               const std::string &outPath,
                               size_t &savedCount)
{
    savedCount = 0;
    if (messages.empty())
        return false;
    FILE *out = fopen(outPath.c_str(), "wb");
    if (!out)
        return false;
    bool ok = true;
    for (GribMessage *gm : messages)
    {
        if (!gm->message)
            gm->message = ReopenGribMessage(*gm);
        if (!gm->message)
        {
            ok = false;
            break;
        }
        const void *buffer = nullptr;
        size_t size = 0;
        int err = codes_get_message(gm->message, &buffer, &size);
        if (!err && buffer && size > 0)
        {
            fwrite(buffer, 1, size, out);
        }
        else
        {
            ok = false;
            break;
        }
    }
    fclose(out);
    if (!ok)
    {
        remove(outPath.c_str());
        return false;
    }
    savedCount = messages.size();
    return true;
}

// ----------------------------------------------------------
// Mouse picking lat/lon
// ----------------------------------------------------------
static double GetLatLonFromMouse(float mx, float my,
                                 float contentX, float contentY,
                                 float &outLat, float &outLon)
{
    if (g_SelectedMessageIndex < 0 || g_SelectedMessageIndex >= (int)g_GribMessages.size())
        return std::numeric_limits<double>::quiet_NaN();
    const GribMessage &gm = g_GribMessages[g_SelectedMessageIndex];
    if (!gm.message || gm.Ni <= 1 || gm.Nj <= 1)
        return std::numeric_limits<double>::quiet_NaN();
    float rx = mx - contentX - g_OffsetX;
    float ry = my - contentY - g_OffsetY;
    float di = rx / g_Zoom;
    float dj = ry / g_Zoom;
    if (di < 0.f || dj < 0.f || di > (float)(gm.Ni - 1) || dj > (float)(gm.Nj - 1))
        return std::numeric_limits<double>::quiet_NaN();
    bool desc = (gm.lat1 > gm.lat2);
    double latRange = fabs(gm.lat1 - gm.lat2);
    double lonRange = gm.lon2 - gm.lon1;
    if (lonRange < 0)
        lonRange += 360.0;
    double fi = di / (gm.Ni - 1);
    double fj = dj / (gm.Nj - 1);
    if (!desc)
        outLat = gm.lat1 + fj * latRange;
    else
        outLat = gm.lat1 - fj * latRange;
    outLon = gm.lon1 + fi * lonRange;
    if (outLon < 0)
        outLon += 360.0;
    outLon = fmod(outLon, 360.0);
    std::vector<double> data;
    double minv, maxv;
    GetMessageValuesAndRange(gm.message, data, minv, maxv);
    int idx = ((int)dj) * gm.Ni + (int)di;
    if (idx < 0 || idx >= (int)data.size())
        return std::numeric_limits<double>::quiet_NaN();
    return data[idx];
}

// ----------------------------------------------------------
// Load a GRIB file and append its messages (do not clear previous ones).
// Instead of keeping the handle, we record the file path and the file offset,
// read minimal keys, then delete the handle.
// ----------------------------------------------------------
static void LoadGribFileAppend(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return;
    codes_handle *h = nullptr;
    int err = 0;
    const int headers_only = 0;
    bool loadedAny = false;
    while ((h = grib_new_from_file(nullptr, f,  headers_only, &err)) != nullptr)
    {
        GribMessage gm;
        gm.index = g_GribMessages.size() + 1;
        gm.filePath = path;
        long offset = 0;
        if (codes_get_long(h, "offset", &offset) == 0)
            gm.fileOffset = offset;
        else
            gm.fileOffset = ftell(f);
        // Retrieve essential fields:
        codes_get_long(h, "level", &gm.level);
        codes_get_long(h, "dataTime", &gm.dataTime);
        codes_get_long(h, "dataDate", &gm.dataDate);
        codes_get_long(h, "Ni", &gm.Ni);
        codes_get_long(h, "Nj", &gm.Nj);
        codes_get_double(h, "latitudeOfFirstGridPointInDegrees", &gm.lat1);
        codes_get_double(h, "latitudeOfLastGridPointInDegrees", &gm.lat2);
        codes_get_double(h, "longitudeOfFirstGridPointInDegrees", &gm.lon1);
        codes_get_double(h, "longitudeOfLastGridPointInDegrees", &gm.lon2);
        char snBuf[64];
        size_t snLen = sizeof(snBuf);
        if (codes_get_string(h, "shortName", snBuf, &snLen) == 0)
            gm.shortName = snBuf;
        char puBuf[128];
        size_t puLen = sizeof(puBuf);
        if (codes_get_string(h, "parameterUnits", puBuf, &puLen) == 0)
            gm.parameterUnits = puBuf;
        char pnBuf[256];
        size_t pnLen = sizeof(pnBuf);
        if (codes_get_string(h, "parameterName", pnBuf, &pnLen) == 0)
            gm.parameterName = pnBuf;
        gm.minVal = 0.0;
        gm.maxVal = 0.0;
        gm.keyValueMap["index"] = std::to_string(gm.index);
        gm.keyValueMap["level"] = std::to_string(gm.level);
        gm.keyValueMap["shortName"] = gm.shortName;
        gm.keyValueMap["dataTime"] = std::to_string(gm.dataTime);
        gm.keyValueMap["dataDate"] = std::to_string(gm.dataDate);
        gm.keyValueMap["Ni"] = std::to_string(gm.Ni);
        gm.keyValueMap["Nj"] = std::to_string(gm.Nj);
        std::vector<std::string> keys = {"startStep", "endStep", "stepRange", "validityDate", "validityTime"};

        for (const auto& key : keys) {
            long tmpVal;
            if (codes_get_long(h, key.c_str(), &tmpVal) == 0) {
                gm.keyValueMap[key] = std::to_string(tmpVal);
            }
        }
        g_GribMessages.push_back(gm);
        loadedAny = true;
    /*    std::cout << "Loaded GRIB #" << gm.index << ": shortName="
                  << gm.shortName << " level=" << gm.level
                  << " date/time=" << gm.dataDate << "/" << gm.dataTime
                  << " Ni/Nj=" << gm.Ni << "/" << gm.Nj << std::endl;*/
        codes_handle_delete(h);
        h = nullptr;
    }
    fclose(f);
    if (loadedAny)
    {
        namespace fs = std::filesystem;
        fs::path parent = fs::path(path).parent_path();
        if (parent.empty())
            parent = fs::current_path();
        UpdateSavePathsForDir(parent);
        UpdateWindowTitle();
    }
}

// ----------------------------------------------------------
// Main
// ----------------------------------------------------------
int main(int argc, char **argv)
{
#if defined(_WIN32)
    (void)freopen("NUL", "w", stdout);
    (void)freopen("NUL", "w", stderr);
#else
    (void)freopen("/dev/null", "w", stdout);
    (void)freopen("/dev/null", "w", stderr);
#endif
    // Initialize SDL and OpenGL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
        return 0;
    ConfigureEcCodesEnvironment();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    std::string windowTitle = std::string("Gribview - lightweight grib viewer built on ECCodes - CNRS 2024 - Firecaster.org");
    g_Window = SDL_CreateWindow(windowTitle.c_str(),
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                g_WindowWidth, g_WindowHeight,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_Window)
        return 0;
    g_GLContext = SDL_GL_CreateContext(g_Window);
    
    
    if (!g_GLContext)
        return 0;
    SDL_GL_SetSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        return 0;
    glGetError();
    UpdateSavePathsForDir(GetHomeDirectory());
    // Load each file provided on the command line (appending messages)
    if (argc > 1)
    {
        std::vector<std::string> initialPaths;
        for (int i = 1; i < argc; i++)
        {
            initialPaths.emplace_back(argv[i]);
        }
        LoadFilesAndSelect(initialPaths);
        if (!g_GribMessages.empty())
        {
            // Set initial zoom so the image fills the available canvas width.
            float canvasWidth = (float)g_WindowWidth - 350.0f; // left panel fixed at 350 px
            if (g_TexWidth > 0)
            {
                g_Zoom = canvasWidth / (float)g_TexWidth;
                g_OffsetX = 0;
                g_OffsetY = 0;
            }
        }
    }
    else
    {
        g_SelectedMessageIndex = -1;
        g_LastSelectionAnchor = -1;
    }
    // ImGui initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable automatic saving/loading of imgui.ini
    
    (void)io;
    ImGui::StyleColorsDark();
    const char *glsl_version = "#version 150";
    ImGui_ImplSDL2_InitForOpenGL(g_Window, g_GLContext);
    ImGui_ImplOpenGL3_Init(glsl_version);
    // Default table columns (index is always shown)
    g_UiState.displayedKeys.push_back("index");
    g_UiState.displayedKeys.push_back("level");
    g_UiState.displayedKeys.push_back("shortName");
    g_UiState.displayedKeys.push_back("dataDate");
    bool done = false;
    static std::vector<std::string> colormapNames;
    while (!done)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_DROPFILE)
            {
                if (ev.drop.file)
                {
                    std::string dropped = ev.drop.file;
                    SDL_free(ev.drop.file);
                    LoadFilesAndSelect({dropped});
                }
                continue;
            }
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT)
                done = true;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                ev.window.windowID == SDL_GetWindowID(g_Window))
                done = true;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                g_WindowWidth = ev.window.data1;
                g_WindowHeight = ev.window.data2;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiIO &ioFrame = ImGui::GetIO();
        bool superDown = ioFrame.KeyCtrl || ioFrame.KeySuper;
        if (superDown && ImGui::IsKeyPressed(ImGuiKey_O))
            g_RequestFileDialog = true;
        if (superDown && ImGui::IsKeyPressed(ImGuiKey_Q))
            done = true;
        if (superDown && ioFrame.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W))
        {
            if (!g_GribMessages.empty())
                ClearAllMessages();
        }
        float menuBarHeight = 0.f;
        if (ImGui::BeginMainMenuBar())
        {
            menuBarHeight = ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open...", "Cmd+O"))
                    g_RequestFileDialog = true;
                bool canClear = !g_GribMessages.empty();
                if (ImGui::MenuItem("Clear Loaded Files", "Cmd+Shift+W", false, canClear))
                {
                    ClearAllMessages();
                }
                if (ImGui::MenuItem("Quit", "Cmd+Q"))
                    done = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        PromptFileDialogIfNeeded();
        StepMarkerExtraction();
        // Left panel
        float leftPanelHeight = (float)g_WindowHeight - menuBarHeight;
        ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, leftPanelHeight), ImGuiCond_Always);
        ImGui::Begin("LeftPanel", nullptr,
                     ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar);
        // Color scale settings
        ImGui::Text("Color Scale");
        ImGui::InputFloat("Min", &g_UserMinVal);
        ImGui::InputFloat("Max", &g_UserMaxVal);
        ImGui::Checkbox("Auto-Fit", &g_AutoFit);
        if (ImGui::Button("Apply##colsc"))
            GenerateTextureForSelectedMessage();
        ImGui::SameLine();
        if (ImGui::Button("Refit Data##colsc"))
        {
            g_AutoFit = true;
            GenerateTextureForSelectedMessage();
        }
        ImGui::Separator();
        // Colormap selection
        if (colormapNames.empty())
        {
            for (auto &kv : colormapMap)
                colormapNames.push_back(kv.first);
            std::sort(colormapNames.begin(), colormapNames.end());
        }
        static int currentComboIdx = 0;
        for (int c = 0; c < (int)colormapNames.size(); c++)
        {
            if (colormapNames[c] == g_ChosenColormapName)
            {
                currentComboIdx = c;
                break;
            }
        }
        ImGui::Text("Colormap:");
        if (ImGui::BeginCombo("##cmap", g_ChosenColormapName.c_str()))
        {
            for (int c = 0; c < (int)colormapNames.size(); c++)
            {
                bool sel = (c == currentComboIdx);
                if (ImGui::Selectable(colormapNames[c].c_str(), sel))
                {
                    currentComboIdx = c;
                    g_ChosenColormapName = colormapNames[c];
                    GenerateTextureForSelectedMessage();
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
        ImGui::Text("PNG Output");
        ImGui::PushItemWidth(-110.0f);
        ImGui::InputText("##pngpath", g_SaveImagePath, IM_ARRAYSIZE(g_SaveImagePath));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse##pngpath"))
        {
            const char *patterns[] = {"*.png"};
            const char *choice = tinyfd_saveFileDialog("Save PNG", g_SaveImagePath, 1, patterns, "PNG files");
            if (choice)
                SetPathBuffer(g_SaveImagePath, IM_ARRAYSIZE(g_SaveImagePath), choice);
        }
        if (ImGui::Button("Save PNG"))
            SaveCurrentImagePNG(g_SaveImagePath);
        ImGui::Separator();
        ImGui::Text("Selection Output");
        int selectedCount = 0;
        for (auto &msg : g_GribMessages)
        {
            if (msg.selected)
                selectedCount++;
        }
        if (selectedCount > 0)
            ImGui::Text("%d message%s selected", selectedCount, selectedCount > 1 ? "s" : "");
        else
            ImGui::Text("No messages selected");
        ImGui::PushItemWidth(-110.0f);
        ImGui::InputText("##gribpath", g_SaveGribPath, IM_ARRAYSIZE(g_SaveGribPath));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse##gribpath"))
        {
            const char *patterns[] = {"*.grib", "*.grb", "*.grib2", "*.grb2"};
            const char *choice = tinyfd_saveFileDialog("Save GRIB selection", g_SaveGribPath, 4, patterns, "GRIB files");
            if (choice)
                SetPathBuffer(g_SaveGribPath, IM_ARRAYSIZE(g_SaveGribPath), choice);
        }
        ImGui::BeginDisabled(selectedCount == 0);
        if (ImGui::Button("Save selection"))
        {
            std::vector<GribMessage *> toWrite;
            toWrite.reserve(selectedCount);
            for (auto &msg : g_GribMessages)
            {
                if (msg.selected)
                    toWrite.push_back(&msg);
            }
            size_t saved = 0;
            bool ok = SaveMessagesToGrib(toWrite, g_SaveGribPath, saved);
            g_SaveSelectionSuccess = ok;
            if (ok)
                g_SaveSelectionStatus = "Saved " + std::to_string(saved) + " message(s) to " + std::string(g_SaveGribPath);
            else
                g_SaveSelectionStatus = "Failed to save selection. Check file permissions.";
        }
        ImGui::EndDisabled();
        if (!g_SaveSelectionStatus.empty())
        {
            ImVec4 color = g_SaveSelectionSuccess ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f)
                                                  : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            ImGui::TextColored(color, "%s", g_SaveSelectionStatus.c_str());
        }
        ImGui::Separator();
        ImGui::Text("Markers");
        if (ImGui::Button("Add marker"))
        {
            g_AddMarkerMode = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Run extraction"))
        {
            StartMarkerExtraction();
        }
        ImGui::SameLine();
        if (!g_ExtractionStatus.empty())
            ImGui::Text("%s", g_ExtractionStatus.c_str());
        ImGui::Spacing();
        if (g_AddMarkerMode)
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "Click on the map to place the marker");
        if (g_Markers.empty())
            ImGui::Text("No markers yet");
        else
        {
            GribMessage *selectedGM = nullptr;
            std::vector<double> selectedData;
            bool selectedHasData = false;
            if (g_SelectedMessageIndex >= 0 && g_SelectedMessageIndex < (int)g_GribMessages.size())
            {
                selectedGM = &g_GribMessages[g_SelectedMessageIndex];
                selectedHasData = LoadMessageData(*selectedGM, selectedData);
            }
            for (size_t i = 0; i < g_Markers.size(); i++)
            {
                auto &m = g_Markers[i];
                ImGui::PushID((int)i);
                ImVec4 c = MarkerColorVec4((int)i);
                char valBuf[32];
                bool valOk = false;
                double val = 0.0;
                if (selectedHasData && selectedGM)
                {
                    if (SampleValueFromData(*selectedGM, selectedData, m.lat, m.lon, val))
                        valOk = true;
                }
                if (valOk)
                    snprintf(valBuf, sizeof(valBuf), "%.2f", val);
                else
                    snprintf(valBuf, sizeof(valBuf), "N/A");
                ImGui::TextColored(c, "Marker %d  lat=%.2f lon=%.2f val=%s", m.id, m.lat, m.lon, valBuf);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    RemoveMarkerAt(i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::Text("Series");
            ImVec2 plotSize(ImGui::GetContentRegionAvail().x, 140.0f);
            DrawMarkersPlot(plotSize);
            if (g_PlotClickRequest >= 0 && g_PlotClickRequest < (int)g_GribMessages.size())
            {
                ClearAllSelections();
                g_GribMessages[g_PlotClickRequest].selected = true;
                g_LastSelectionAnchor = g_PlotClickRequest;
                RefreshSelectionState(true, g_PlotClickRequest);
                g_ExtractionRunning = false;
                g_ExtractionStatus = "";
            }
            ImGui::PushItemWidth(-150.0f);
            ImGui::InputText("##markerscsv", g_MarkersCsvPath, IM_ARRAYSIZE(g_MarkersCsvPath));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Browse##markerscsv"))
            {
                const char *patterns[] = {"*.csv"};
                const char *choice = tinyfd_saveFileDialog("Save markers CSV", g_MarkersCsvPath, 1, patterns, "CSV files");
                if (choice)
                    SetPathBuffer(g_MarkersCsvPath, IM_ARRAYSIZE(g_MarkersCsvPath), choice);
            }
            ImGui::SameLine();
            if (ImGui::Button("Save CSV##allmarkers"))
            {
                bool ok = SaveMarkersCsv();
                g_ExtractionStatus = ok ? "CSV saved" : "Failed to save CSV";
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Keys Select"))
            ImGui::OpenPopup("KeysSelectPopup");
        ImGui::SameLine();
        if (ImGui::Button("Properties"))
        {
            if (g_SelectedMessageIndex >= 0 && g_SelectedMessageIndex < (int)g_GribMessages.size())
            {
                g_ShowInspector = true;
                g_InspectorIndex = g_SelectedMessageIndex;
            }
        }

        ImGui::Separator();
        // Keys Select Popup
        if (ImGui::BeginPopupModal("KeysSelectPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Select keys to display:");
            ImGui::Text("index (always shown)");
            if (g_UiState.availableKeys.empty() && !g_GribMessages.empty())
            {
                for (auto &pair : g_GribMessages[0].keyValueMap)
                {
                    std::string key = pair.first;
                    if (key == "values" || key == "bitmap" || key == "pv" || key == "mask")
                        continue;
                    if (key == "index")
                        continue;
                    g_UiState.availableKeys.push_back(key);
                }
                std::sort(g_UiState.availableKeys.begin(), g_UiState.availableKeys.end());
            }
            for (auto &key : g_UiState.availableKeys)
            {
                bool isChecked = (std::find(g_UiState.displayedKeys.begin(), g_UiState.displayedKeys.end(), key) != g_UiState.displayedKeys.end());
                bool temp = isChecked;
                if (ImGui::Checkbox(key.c_str(), &temp))
                {
                    if (temp && !isChecked)
                        g_UiState.displayedKeys.push_back(key);
                    else if (!temp && isChecked)
                    {
                        auto it = std::find(g_UiState.displayedKeys.begin(), g_UiState.displayedKeys.end(), key);
                        if (it != g_UiState.displayedKeys.end())
                            g_UiState.displayedKeys.erase(it);
                    }
                }
            }
            if (ImGui::Button("Close##keysselect"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();
        // Table of GRIB messages
        if (g_UiState.displayedKeys.empty())
        {
            ImGui::Text("No columns. Use 'Keys Select' to select columns.");
        }
        else
        {
            if (ImGui::BeginTable("GribTable", (int)g_UiState.displayedKeys.size(),
                                  ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_Reorderable |
                                      ImGuiTableFlags_Sortable |
                                      ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                for (int col = 0; col < (int)g_UiState.displayedKeys.size(); col++)
                {
                    ImGui::TableSetupColumn(g_UiState.displayedKeys[col].c_str(),
                                            ImGuiTableColumnFlags_WidthStretch);
                }
                ImGui::TableHeadersRow();
                if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs())
                {
            if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs *spec = &sortSpecs->Specs[0];
                g_UiState.sortKey = g_UiState.displayedKeys[spec->ColumnIndex];
                g_UiState.sortAscending = (spec->SortDirection == ImGuiSortDirection_Ascending);
                std::stable_sort(g_GribMessages.begin(), g_GribMessages.end(),
                                 [&](const GribMessage &ma, const GribMessage &mb)
                                 {
                                     return compareGribMessages(ma, mb,
                                                                g_UiState.sortKey,
                                                                g_UiState.sortAscending);
                                 });
                ClearMarkerSeries();
                g_ExtractionRunning = false;
                g_ExtractionStatus = "Markers cleared after reordering";
                int activeIndex = -1;
                for (size_t j = 0; j < g_GribMessages.size(); j++)
                {
                    if (g_GribMessages[j].selected)
                    {
                                activeIndex = j;
                                break;
                            }
                        }
                        g_SelectedMessageIndex = activeIndex;
                        g_LastSelectionAnchor = g_SelectedMessageIndex;
                        sortSpecs->SpecsDirty = false;
                    }
                }
                bool tableFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
                if (tableFocused)
                {
                    const ImGuiIO &io = ImGui::GetIO();
                    bool shiftDown = io.KeyShift;
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    {
                        if (!g_GribMessages.empty())
                        {
                            int current = (g_SelectedMessageIndex >= 0) ? g_SelectedMessageIndex : 0;
                            int newIndex = std::max(0, current - 1);
                            if (shiftDown)
                            {
                                if (g_LastSelectionAnchor < 0)
                                    g_LastSelectionAnchor = current;
                                SelectRangeInclusive(g_LastSelectionAnchor, newIndex);
                            }
                            else
                            {
                                ClearAllSelections();
                                g_GribMessages[newIndex].selected = true;
                                g_LastSelectionAnchor = newIndex;
                            }
                            RefreshSelectionState(true, newIndex);
                            if (!shiftDown && g_SelectedMessageIndex >= 0)
                                g_LastSelectionAnchor = g_SelectedMessageIndex;
                        }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    {
                        if (!g_GribMessages.empty())
                        {
                            int lastIndex = (int)g_GribMessages.size() - 1;
                            int current = (g_SelectedMessageIndex >= 0) ? g_SelectedMessageIndex : -1;
                            int newIndex = (current < 0) ? 0 : std::min(current + 1, lastIndex);
                            if (shiftDown)
                            {
                                if (g_LastSelectionAnchor < 0)
                                    g_LastSelectionAnchor = (current >= 0) ? current : newIndex;
                                SelectRangeInclusive(g_LastSelectionAnchor, newIndex);
                            }
                            else
                            {
                                ClearAllSelections();
                                g_GribMessages[newIndex].selected = true;
                                g_LastSelectionAnchor = newIndex;
                            }
                            RefreshSelectionState(true, newIndex);
                            if (!shiftDown && g_SelectedMessageIndex >= 0)
                                g_LastSelectionAnchor = g_SelectedMessageIndex;
                        }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
                    {
                        std::vector<GribMessage> remaining;
                        for (auto &msg : g_GribMessages)
                        {
                            if (msg.selected)
                            {
                                if (msg.message)
                                    codes_handle_delete(msg.message);
                            }
                            else
                                remaining.push_back(msg);
                        }
                        g_GribMessages = remaining;
                        for (size_t i = 0; i < g_GribMessages.size(); i++)
                        {
                            g_GribMessages[i].index = i + 1;
                        }
                        if (!g_GribMessages.empty())
                        {
                            for (auto &msg : g_GribMessages)
                                msg.selected = false;
                            g_GribMessages[0].selected = true;
                            g_SelectedMessageIndex = 0;
                            GenerateTextureForSelectedMessage();
                            g_LastSelectionAnchor = 0;
                            ClearMarkerSeries();
                            g_ExtractionRunning = false;
                            g_ExtractionStatus = "Markers cleared after delete";
                        }
                        else
                        {
                            g_SelectedMessageIndex = -1;
                            g_LastSelectionAnchor = -1;
                            DestroyTexture(g_TextureID);
                            ClearMarkerSeries();
                            g_ExtractionRunning = false;
                            g_ExtractionStatus = "Markers cleared after delete";
                        }
                    }
                }
                for (size_t i = 0; i < g_GribMessages.size(); i++)
                {
                    GribMessage &gm = g_GribMessages[i];
                    bool isSelected = gm.selected;
                    ImGui::TableNextRow(ImGuiTableRowFlags_None);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID((int)i);
                    if (ImGui::Selectable("##rowSel", isSelected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
                    {
                        ImGuiIO &rowIO = ImGui::GetIO();
                        bool shiftDown = rowIO.KeyShift;
                        bool toggleModifier = rowIO.KeyCtrl || rowIO.KeySuper;
                        int clickedIndex = (int)i;
                        if (shiftDown)
                        {
                            if (g_LastSelectionAnchor < 0)
                                g_LastSelectionAnchor = (g_SelectedMessageIndex >= 0) ? g_SelectedMessageIndex : clickedIndex;
                            SelectRangeInclusive(g_LastSelectionAnchor, clickedIndex);
                            RefreshSelectionState(true, clickedIndex);
                        }
                        else if (toggleModifier)
                        {
                            gm.selected = !gm.selected;
                            RefreshSelectionState(true, gm.selected ? clickedIndex : -1);
                            if (gm.selected)
                                g_LastSelectionAnchor = clickedIndex;
                            else if (g_SelectedMessageIndex < 0)
                                g_LastSelectionAnchor = -1;
                        }
                        else
                        {
                            ClearAllSelections();
                            gm.selected = true;
                            g_LastSelectionAnchor = clickedIndex;
                            RefreshSelectionState(true, clickedIndex);
                        }
                    }
                    ImGui::PopID();
                    ImGui::SameLine();
                    auto it = gm.keyValueMap.find(g_UiState.displayedKeys[0]);
                    if (it != gm.keyValueMap.end())
                        ImGui::TextUnformatted(it->second.c_str());
                    for (int col = 1; col < (int)g_UiState.displayedKeys.size(); col++)
                    {
                        ImGui::TableSetColumnIndex(col);
                        const std::string &ky = g_UiState.displayedKeys[col];
                        auto it2 = gm.keyValueMap.find(ky);
                        if (it2 != gm.keyValueMap.end())
                            ImGui::TextUnformatted(it2->second.c_str());
                        else
                            ImGui::TextUnformatted("");
                    }
                    if (g_ScrollPendingIndex == (int)i)
                    {
                        ImGui::SetScrollHereY();
                        g_ScrollPendingIndex = -1;
                    }
                }
                ImGui::EndTable();
                if (g_ScrollPendingIndex >= 0)
                    g_ScrollPendingIndex = -1;
            }
        }
        ImGui::End(); // End left panel
        // Right panel: Canvas for displaying the field
        ImGui::SetNextWindowPos(ImVec2(350.0f, menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)g_WindowWidth - 350.0f, (float)g_WindowHeight - menuBarHeight), ImGuiCond_Always);
        ImGui::Begin("FieldCanvas", NULL,
                     ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float cw = avail.x;
        float ch = avail.y - 30.f; // space for status bar
        ImGui::BeginChild("Canvas", ImVec2(cw, ch), true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImVec2 mp = ImGui::GetIO().MousePos;
        float canvasWidth = (float)g_WindowWidth - 350.0f; 
        float canvasHeight = (float)g_WindowHeight - 30.0f; 
        if (ImGui::IsWindowHovered())
        {
            float w = ImGui::GetIO().MouseWheel;
            if (fabs(w) > 1e-5f)
            {
                float oldZ = g_Zoom;
                g_Zoom = g_Zoom * (1.f + 0.1f * w);
                float minZoomX = canvasWidth/ (float)g_TexWidth;
                float minZoomY = canvasHeight / (float)g_TexHeight;
                float minZoom = std::min(minZoomX, minZoomY);
                g_Zoom = std::max(g_Zoom, minZoom);

                float dx = mp.x - cp.x - g_OffsetX;
                float dy = mp.y - cp.y - g_OffsetY;
                float s = g_Zoom / oldZ;

                g_OffsetX = mp.x - cp.x - dx * s;
                g_OffsetY = mp.y - cp.y - dy * s;

                if (fabs(g_Zoom - minZoom) < 1e-5f)
                {
                   
                    float centerX = (canvasWidth - g_TexWidth * g_Zoom) / 2.0f;
                    float centerY = (canvasHeight - g_TexHeight * g_Zoom) / 2.0f;
                    g_OffsetX = (centerX + g_OffsetX) / 2.0f;
                    g_OffsetY = (centerY + g_OffsetY) / 2.0f;
                }
            }
            bool rDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (!g_IsPanning && rDown && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                g_IsPanning = true;
                g_PanStart = mp;
            }
            if (g_IsPanning && !rDown)
                g_IsPanning = false;
            if (g_IsPanning && rDown)
            {
                ImVec2 d(mp.x - g_PanStart.x, mp.y - g_PanStart.y);
                g_PanStart = mp;
                g_OffsetX += d.x;
                g_OffsetY += d.y;
            }
        }
        if (g_TextureID)
        {
            float dw = g_TexWidth * g_Zoom;
            float dh = g_TexHeight * g_Zoom;
            ImVec2 pMin(cp.x + g_OffsetX, cp.y + g_OffsetY);
            ImVec2 pMax(pMin.x + dw, pMin.y + dh);
            dl->AddImage((ImTextureID)(intptr_t)g_TextureID, pMin, pMax);
            GribMessage *activeMsg = (g_SelectedMessageIndex >= 0 && g_SelectedMessageIndex < (int)g_GribMessages.size()) ? &g_GribMessages[g_SelectedMessageIndex] : nullptr;
            bool overImage = (mp.x >= pMin.x && mp.x <= pMax.x && mp.y >= pMin.y && mp.y <= pMax.y);
            if (ImGui::IsWindowHovered() && overImage)
            {
                if (g_AddMarkerMode && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    float latC = 0.0f, lonC = 0.0f;
                    double vPick = GetLatLonFromMouse(mp.x, mp.y, cp.x, cp.y, latC, lonC);
                    if (!std::isnan(vPick))
                    {
                        CreateMarkerAt((double)latC, (double)lonC);
                        g_AddMarkerMode = false;
                    }
                }
                if (!g_AddMarkerMode && g_DraggingMarkerIndex < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    for (int idx = (int)g_Markers.size() - 1; idx >= 0; --idx)
                    {
                        ImVec2 pos;
                        if (activeMsg && LatLonToScreen(*activeMsg, g_Markers[idx].lat, g_Markers[idx].lon, cp.x, cp.y, pos))
                        {
                            float dx = mp.x - pos.x;
                            float dy = mp.y - pos.y;
                            if ((dx * dx + dy * dy) <= 12.0f * 12.0f)
                            {
                                g_DraggingMarkerIndex = idx;
                                break;
                            }
                        }
                    }
                }
                if (g_DraggingMarkerIndex >= 0)
                {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        float latC = 0.0f, lonC = 0.0f;
                        double vPick = GetLatLonFromMouse(mp.x, mp.y, cp.x, cp.y, latC, lonC);
                        if (!std::isnan(vPick))
                        {
                            g_Markers[g_DraggingMarkerIndex].lat = (double)latC;
                            g_Markers[g_DraggingMarkerIndex].lon = (double)lonC;
                        }
                    }
                    else
                    {
                        g_DraggingMarkerIndex = -1;
                    }
                }
            }
            if (activeMsg)
            {
                for (size_t mi = 0; mi < g_Markers.size(); mi++)
                {
                    const Marker &m = g_Markers[mi];
                    ImVec2 pos;
                    if (!LatLonToScreen(*activeMsg, m.lat, m.lon, cp.x, cp.y, pos))
                        continue;
                    ImU32 col = MarkerColor((int)mi);
                    ImU32 border = IM_COL32(30, 30, 30, 255);
                    dl->AddCircleFilled(pos, 6.5f, col);
                    dl->AddCircle(pos, 8.0f, border, 20, 2.0f);
                    std::string label = "M" + std::to_string(m.id);
                    dl->AddText(ImVec2(pos.x + 10, pos.y - 10), IM_COL32(240, 240, 240, 255), label.c_str());
                }
            }
            if (ImGui::IsWindowHovered())
            {
                dl->AddLine(ImVec2(mp.x, pMin.y), ImVec2(mp.x, pMax.y),
                            IM_COL32(255, 255, 255, 128));
                dl->AddLine(ImVec2(pMin.x, mp.y), ImVec2(pMax.x, mp.y),
                            IM_COL32(255, 255, 255, 128));
            }
        }
        ImGui::EndChild();
        if (g_DraggingMarkerIndex >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            g_DraggingMarkerIndex = -1;
        // Status bar
        ImVec2 sbPos = ImGui::GetCursorScreenPos();
        float sbH = 30.f;
        ImGui::InvisibleButton("StatusBar", ImVec2(avail.x, sbH));
        ImDrawList *sbDL = ImGui::GetWindowDrawList();
        ImVec2 sbMin = sbPos;
        ImVec2 sbMax(sbPos.x + avail.x, sbPos.y + sbH);
        sbDL->AddRectFilled(sbMin, sbMax, IM_COL32(50, 50, 50, 255));
        float latVal = 0.f, lonVal = 0.f;
        double valPick = std::numeric_limits<double>::quiet_NaN();
        if (g_TextureID)
            valPick = GetLatLonFromMouse(mp.x, mp.y, cp.x, cp.y, latVal, lonVal);
        std::string valMeta;
        if (g_SelectedMessageIndex >= 0 && g_SelectedMessageIndex < (int)g_GribMessages.size())
        {
            GribMessage &gmSel = g_GribMessages[g_SelectedMessageIndex];
            EnsureParameterInfo(gmSel);
            if (!gmSel.parameterName.empty())
                valMeta += " " + gmSel.parameterName;
            if (!gmSel.parameterUnits.empty())
                valMeta += " [" + gmSel.parameterUnits + "]";
        }
        char sbText[256];
        if (!std::isnan(valPick))
        {
            if (((latVal - 41.5) * (42.2 - latVal) > 0) &&
                ((lonVal - 8.5) * (9.5 - lonVal) > 0))
            {
                snprintf(sbText, sizeof(sbText),
                         "Lat=%.3f Lon=%.3f Val=%.3f%s  Forza Corsica, center of the world !",
                         latVal, lonVal, (float)valPick, valMeta.c_str());
            }
            else
            {
                snprintf(sbText, sizeof(sbText),
                         "Lat=%.3f Lon=%.3f Val=%.3f%s - Scroll: Zoom, Right Drag: Pan",
                         latVal, lonVal, (float)valPick, valMeta.c_str());
            }
        }
        else
        {
            snprintf(sbText, sizeof(sbText), "Place mouse over the map for value - Scroll: Zoom, Right Drag: Pan");
        }
        sbDL->AddText(ImVec2(sbMin.x + 10, sbMin.y + 7), IM_COL32(255, 255, 255, 255), sbText);
        ImGui::End(); // End right panel
        // Inspector popup (More Info)
        if (g_ShowInspector && g_InspectorIndex >= 0 && g_InspectorIndex < (int)g_GribMessages.size())
        {
            GribMessage &inspMsg = g_GribMessages[g_InspectorIndex];
            if (!inspMsg.message)
                inspMsg.message = ReopenGribMessage(inspMsg);
            PopulateAllKeys(inspMsg);
            ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector", &g_ShowInspector, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Details for message #%d", inspMsg.index);
                ImGui::Separator();
                ImGui::Text("Save this message");
                ImGui::PushItemWidth(-110.0f);
                ImGui::InputText("##singlepath", g_SaveSinglePath, IM_ARRAYSIZE(g_SaveSinglePath));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Browse##singlepath"))
                {
                    const char *patterns[] = {"*.grib", "*.grb", "*.grib2", "*.grb2"};
                    const char *choice = tinyfd_saveFileDialog("Save GRIB message", g_SaveSinglePath, 4, patterns, "GRIB files");
                    if (choice)
                        SetPathBuffer(g_SaveSinglePath, IM_ARRAYSIZE(g_SaveSinglePath), choice);
                }
                if (ImGui::Button("Save message##insp"))
                    SaveSingleMessageGrib(inspMsg, g_SaveSinglePath);
                ImGui::Separator();
                ImVec2 listSize(480, 300);
                ImGui::BeginChild("KeyListInsp", listSize, true);
                for (auto it = inspMsg.keyValueMap.begin(); it != inspMsg.keyValueMap.end(); ++it)
                {
                    ImGui::Text("%s = %s", it->first.c_str(), it->second.c_str());
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }
        // Render
        ImGui::Render();
        glViewport(0, 0, g_WindowWidth, g_WindowHeight);
        glClearColor(0.2f, 0.2f, 0.2f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_Window);
    }
    // Cleanup
    DestroyTexture(g_TextureID);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(g_GLContext);
    SDL_DestroyWindow(g_Window);
    SDL_Quit();
    for (size_t i = 0; i < g_GribMessages.size(); i++)
    {
        if (g_GribMessages[i].message)
        {
            codes_handle_delete(g_GribMessages[i].message);
            g_GribMessages[i].message = NULL;
        }
    }
    return 0;
}
static void ConfigureEcCodesEnvironment()
{
    namespace fs = std::filesystem;
    const char *defsEnv = std::getenv("ECCODES_DEFINITION_PATH");
    const char *samplesEnv = std::getenv("ECCODES_SAMPLES_PATH");
    if (defsEnv && samplesEnv)
        return;
    char *base = SDL_GetBasePath();
    if (!base)
        return;
    std::string baseStr = base;
    SDL_free(base);
    fs::path resources = fs::path(baseStr).append("../Resources/eccodes");
    fs::path defs = resources / "definitions";
    fs::path samples = resources / "samples";
    if (!defsEnv && fs::exists(defs))
    {
#if defined(_WIN32)
        _putenv_s("ECCODES_DEFINITION_PATH", defs.string().c_str());
#else
        setenv("ECCODES_DEFINITION_PATH", defs.string().c_str(), 1);
#endif
    }
    if (!samplesEnv && fs::exists(samples))
    {
#if defined(_WIN32)
        _putenv_s("ECCODES_SAMPLES_PATH", samples.string().c_str());
#else
        setenv("ECCODES_SAMPLES_PATH", samples.string().c_str(), 1);
#endif
    }
}
