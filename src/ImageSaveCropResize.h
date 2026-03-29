/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

enum class ImageEditMode {
    Crop,
    Resize
};

void ShowImageEditWindow(MainWindow* win, ImageEditMode mode);
