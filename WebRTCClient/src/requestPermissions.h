#pragma once

// 请求 AVFoundation 摄像头和麦克风权限，阻塞直到用户响应（或已授权）。
// 实现在 requestPermissions.mm（Objective-C++）。
// 必须在主 run loop 启动后（app.exec() 内）调用，否则对话框无法显示。
void requestAVPermissions();
