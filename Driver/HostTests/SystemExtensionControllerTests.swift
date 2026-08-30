import Foundation

private var failures = 0

private func checkEqual<T: Equatable>(
    _ actual: T,
    _ expected: T,
    _ message: String)
{
    guard actual == expected else {
        fputs("FAIL: \(message): actual=\(actual), expected=\(expected)\n", stderr)
        failures += 1
        return
    }
}

private final class FakeSubmitter: ExtensionRequestSubmitting {
    weak var eventHandler: ExtensionEventHandling?
    var requests: [ExtensionRequestDescriptor] = []

    func submit(_ request: ExtensionRequestDescriptor) {
        requests.append(request)
    }
}

@main
private struct TestRunner {
    static func main() {
        testActivationLifecycle()
        testDeactivationLifecycle()
        testFailureAndRebootStates()
        testRejectsDowngrade()

        if failures == 0 {
            print("PASS SystemExtensionController")
        }
        exit(failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE)
    }

    private static func testActivationLifecycle() {
        let fake = FakeSubmitter()
        let controller = SystemExtensionController(submitter: fake)
        let request = ExtensionRequestDescriptor(
            operation: .activate,
            identifier: SystemExtensionController.driverIdentifier)

        controller.activate()
        checkEqual(fake.requests, [request], "activation request")
        checkEqual(controller.state, .submitting(.activate), "activation submitting state")

        fake.eventHandler?.extensionRequiresApproval(request)
        checkEqual(controller.state, .waitingForApproval, "approval state")

        fake.eventHandler?.extensionRequestFinished(request, rebootRequired: false)
        checkEqual(controller.state, .active, "activation completed state")
    }

    private static func testDeactivationLifecycle() {
        let fake = FakeSubmitter()
        let controller = SystemExtensionController(submitter: fake)
        let request = ExtensionRequestDescriptor(
            operation: .deactivate,
            identifier: SystemExtensionController.driverIdentifier)

        controller.deactivate()
        checkEqual(fake.requests, [request], "deactivation request")
        checkEqual(controller.state, .submitting(.deactivate), "deactivation submitting state")

        fake.eventHandler?.extensionRequestFinished(request, rebootRequired: false)
        checkEqual(controller.state, .inactive, "deactivation completed state")
    }

    private static func testFailureAndRebootStates() {
        let fake = FakeSubmitter()
        let controller = SystemExtensionController(submitter: fake)
        let activation = ExtensionRequestDescriptor(
            operation: .activate,
            identifier: SystemExtensionController.driverIdentifier)

        controller.activate()
        fake.eventHandler?.extensionRequestFailed(activation, message: "code=8 signature invalid")
        checkEqual(
            controller.state,
            .failed("code=8 signature invalid"),
            "failure state")

        controller.activate()
        fake.eventHandler?.extensionRequestFinished(activation, rebootRequired: true)
        checkEqual(
            controller.state,
            .rebootRequired(.activate),
            "reboot-required state")
    }

    private static func testRejectsDowngrade() {
        checkEqual(
            SystemExtensionController.shouldReplace(existingBuild: "9", newBuild: "10"),
            true,
            "newer build replaces")
        checkEqual(
            SystemExtensionController.shouldReplace(existingBuild: "10", newBuild: "10"),
            true,
            "equal development build replaces")
        checkEqual(
            SystemExtensionController.shouldReplace(existingBuild: "10", newBuild: "9"),
            false,
            "older build is rejected")
    }
}
