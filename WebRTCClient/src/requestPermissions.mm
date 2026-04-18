// Objective-C++ 实现：请求 AVFoundation 摄像头和麦克风权限
// 使用 dispatch_semaphore 等待用户响应，completion handler 由 GCD 在后台线程调用，
// 不依赖主线程 run loop（此函数在主线程内通过 QTimer 调用）。
#import <AVFoundation/AVFoundation.h>
#include <dispatch/dispatch.h>

static void waitForPermission(AVMediaType mediaType) {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:mediaType];
    if (status == AVAuthorizationStatusAuthorized) return;  // 已授权，立即返回

    // NotDetermined：弹出对话框，等待用户响应
    // Denied / Restricted：直接返回（无法重新弹框，用户需去系统设置手动开启）
    if (status != AVAuthorizationStatusNotDetermined) return;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [AVCaptureDevice requestAccessForMediaType:mediaType
                             completionHandler:^(BOOL) {
        dispatch_semaphore_signal(sem);
    }];
    // GCD completion handler 在后台线程触发，此处安全等待（不阻塞主 run loop 的 UI 响应，
    // 因为 macOS 权限对话框由系统独立呈现，不依赖 app 的 run loop 迭代）
    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
}

void requestAVPermissions() {
    waitForPermission(AVMediaTypeVideo);
    waitForPermission(AVMediaTypeAudio);
}
