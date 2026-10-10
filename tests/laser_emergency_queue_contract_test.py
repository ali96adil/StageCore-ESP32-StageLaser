from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "stage_device_runtime.cpp").read_text()


def section(start: str, end: str) -> str:
    return SOURCE.split(start, 1)[1].split(end, 1)[0]


class EmergencyOffQueueContract(unittest.TestCase):
    def test_safe_off_and_disarm_preempt_the_full_ordinary_queue(self):
        classifier = section("bool is_emergency_off_frame(", "bool queue_command(")
        self.assertIn('"command.execute"', classifier)
        self.assertIn('"LASER_SAFE_OFF"', classifier)
        self.assertIn('"LASER_DISARM"', classifier)
        queue = section("bool queue_command(", "bool queue_setup_ap_maintenance(")
        self.assertIn("const size_t limit = emergency ? 9 : 8;", queue)
        self.assertIn("++context->command_queue_epoch;", queue)
        self.assertIn("pending_command_frames.insert(", queue)
        self.assertIn("pending_command_frames.begin(), std::move(frame)", queue)
        self.assertIn("pending_command_frames.push_back(std::move(frame))", queue)

    def test_queued_pre_emergency_commands_cannot_actuate_or_retry(self):
        dequeue = section("bool take_command(", "bool take_setup_ap_maintenance(")
        self.assertIn("next.queue_epoch < context->command_queue_epoch", dequeue)
        process = section("std::string command_frame;", "const uint64_t now_ms =")
        self.assertIn("queue_superseded", process)
        self.assertIn("decision.disposition == stagelaser::CommandDisposition::kReady", process)
        self.assertIn("command_replay_remember(", process)
        self.assertIn('"DEVICE_COMMAND_SUPERSEDED"', process)
        self.assertIn("context.journal.Remember(", process)
        self.assertLess(
            process.index("command_replay_remember("),
            process.index('"DEVICE_COMMAND_SUPERSEDED"'),
        )
        self.assertLess(
            process.index('"DEVICE_COMMAND_SUPERSEDED"'),
            process.index("start_ready_command("),
        )

    def test_pending_transition_is_preempted_only_after_durable_replay_fence(self):
        command = section("std::string start_ready_command(", "std::string poll_runtime_command(")
        self.assertIn("state->pending && !emergency_off", command)
        self.assertIn("command_replay_remember(command.command_id)", command)
        self.assertIn("state->pending && emergency_off", command)
        self.assertIn("state->interrupted_response", command)
        self.assertIn('"CANCELLED"', command)
        self.assertIn('"DEVICE_COMMAND_SUPERSEDED"', command)
        self.assertLess(
            command.index("command_replay_remember(command.command_id)"),
            command.index("state->pending && emergency_off"),
        )
        process = section("std::string command_frame;", "const uint64_t now_ms =")
        self.assertIn("command_state.interrupted_response", process)
        self.assertIn("send_text(client, command_state.interrupted_response)", process)

    def test_no_implicit_gpio_actuation_or_weakened_interlocks(self):
        self.assertIn("known_safe_off(*laser)", SOURCE)
        self.assertIn("evaluate_command_execute_frame(", SOURCE)
        self.assertIn("drive_safe_off(laser)", SOURCE)
        self.assertIn("context.commands_enabled", SOURCE)
        self.assertNotIn("STAGECORE_LASER_ACTUATION_ENABLED=1", SOURCE)


if __name__ == "__main__":
    unittest.main()
