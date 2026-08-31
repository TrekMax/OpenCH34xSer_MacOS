import Foundation
import Combine
import SystemExtensions

enum ExtensionOperation: Equatable {
    case activate
    case deactivate
}

struct ExtensionRequestDescriptor: Equatable {
    let operation: ExtensionOperation
    let identifier: String
}

enum DriverExtensionState: Equatable {
    case inactive
    case submitting(ExtensionOperation)
    case waitingForApproval
    case active
    case rebootRequired(ExtensionOperation)
    case failed(String)
}

protocol ExtensionEventHandling: AnyObject {
    func extensionRequiresApproval(_ request: ExtensionRequestDescriptor)
    func extensionRequestFinished(
        _ request: ExtensionRequestDescriptor,
        rebootRequired: Bool)
    func extensionRequestFailed(
        _ request: ExtensionRequestDescriptor,
        message: String)
}

protocol ExtensionRequestSubmitting: AnyObject {
    var eventHandler: ExtensionEventHandling? { get set }
    func submit(_ request: ExtensionRequestDescriptor)
}

final class SystemExtensionController: ObservableObject, ExtensionEventHandling {
    static let driverIdentifier = "com.trekmax.OpenCH34xSer.driver"

    private let submitter: ExtensionRequestSubmitting
    private var pendingRequest: ExtensionRequestDescriptor?
    @Published private(set) var state: DriverExtensionState = .inactive

    convenience init() {
        self.init(submitter: NativeSystemExtensionSubmitter())
    }

    init(submitter: ExtensionRequestSubmitting) {
        self.submitter = submitter
        submitter.eventHandler = self
    }

    func activate() {
        submit(.activate)
    }

    func deactivate() {
        submit(.deactivate)
    }

    func extensionRequiresApproval(_ request: ExtensionRequestDescriptor) {
        guard request == pendingRequest else {
            return
        }
        state = .waitingForApproval
    }

    func extensionRequestFinished(
        _ request: ExtensionRequestDescriptor,
        rebootRequired: Bool)
    {
        guard request == pendingRequest else {
            return
        }
        pendingRequest = nil
        if rebootRequired {
            state = .rebootRequired(request.operation)
        } else {
            state = request.operation == .activate ? .active : .inactive
        }
    }

    func extensionRequestFailed(
        _ request: ExtensionRequestDescriptor,
        message: String)
    {
        guard request == pendingRequest else {
            return
        }
        pendingRequest = nil
        state = .failed(message)
    }

    static func shouldReplace(existingBuild: String, newBuild: String) -> Bool {
        newBuild.compare(existingBuild, options: .numeric) != .orderedAscending
    }

    private func submit(_ operation: ExtensionOperation) {
        guard pendingRequest == nil else {
            return
        }
        let request = ExtensionRequestDescriptor(
            operation: operation,
            identifier: Self.driverIdentifier)
        pendingRequest = request
        state = .submitting(operation)
        submitter.submit(request)
    }
}

final class NativeSystemExtensionSubmitter: NSObject, ExtensionRequestSubmitting {
    weak var eventHandler: ExtensionEventHandling?
    private var requests: [ObjectIdentifier: ExtensionRequestDescriptor] = [:]

    func submit(_ descriptor: ExtensionRequestDescriptor) {
        let request: OSSystemExtensionRequest
        switch descriptor.operation {
        case .activate:
            request = .activationRequest(
                forExtensionWithIdentifier: descriptor.identifier,
                queue: .main)
        case .deactivate:
            request = .deactivationRequest(
                forExtensionWithIdentifier: descriptor.identifier,
                queue: .main)
        }

        request.delegate = self
        requests[ObjectIdentifier(request)] = descriptor
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    private func descriptor(
        for request: OSSystemExtensionRequest,
        remove: Bool) -> ExtensionRequestDescriptor?
    {
        let key = ObjectIdentifier(request)
        if remove {
            return requests.removeValue(forKey: key)
        }
        return requests[key]
    }
}

extension NativeSystemExtensionSubmitter: OSSystemExtensionRequestDelegate {
    func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension ext: OSSystemExtensionProperties)
        -> OSSystemExtensionRequest.ReplacementAction
    {
        SystemExtensionController.shouldReplace(
            existingBuild: existing.bundleVersion,
            newBuild: ext.bundleVersion) ? .replace : .cancel
    }

    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        guard let descriptor = descriptor(for: request, remove: false) else {
            return
        }
        eventHandler?.extensionRequiresApproval(descriptor)
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result)
    {
        guard let descriptor = descriptor(for: request, remove: true) else {
            return
        }
        eventHandler?.extensionRequestFinished(
            descriptor,
            rebootRequired: result == .willCompleteAfterReboot)
    }

    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        guard let descriptor = descriptor(for: request, remove: true) else {
            return
        }
        let error = error as NSError
        eventHandler?.extensionRequestFailed(
            descriptor,
            message: "\(error.domain) code=\(error.code): \(error.localizedDescription)")
    }
}
