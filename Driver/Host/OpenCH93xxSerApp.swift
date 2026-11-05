import SwiftUI

@main
struct OpenCH93xxSerApp: App {
    @StateObject private var controller = SystemExtensionController()
    private let appName = Bundle.main.object(forInfoDictionaryKey: "CFBundleDisplayName") as? String
        ?? ProcessInfo.processInfo.processName

    var body: some Scene {
        WindowGroup(appName) {
            VStack(spacing: 12) {
                Text(appName)
                    .font(.title)
                Text(statusText)
                    .textSelection(.enabled)
                HStack {
                    Button("激活驱动") {
                        controller.activate()
                    }
                    Button("停用驱动") {
                        controller.deactivate()
                    }
                }
            }
            .padding(32)
            .frame(minWidth: 460, minHeight: 180)
        }
    }

    private var statusText: String {
        switch controller.state {
        case .inactive:
            return "驱动未激活"
        case .submitting(.activate):
            return "正在提交激活请求…"
        case .submitting(.deactivate):
            return "正在提交停用请求…"
        case .waitingForApproval:
            return "等待在系统设置中批准驱动"
        case .active:
            return "驱动已激活"
        case .rebootRequired(.activate):
            return "重启后完成驱动激活"
        case .rebootRequired(.deactivate):
            return "重启后完成驱动停用"
        case .failed(let message):
            return "操作失败：\(message)"
        }
    }
}
