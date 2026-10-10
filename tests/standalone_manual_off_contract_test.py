from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]

class StandaloneLampManualOffContract(unittest.TestCase):
    def setUp(self):
        self.cpp=(ROOT/"src"/"stage_device_runtime.cpp").read_text()

    def test_authenticated_unassigned_only_and_boot_fenced(self):
        s=self.cpp
        for marker in (
            "stagelamp.maintenance.manual-off",
            'context->assignment_state == "UNASSIGNED"',
            'context->assignment_state != "UNASSIGNED"',
            'observed_boot_id == boot_id()',
            'generation == context->connection_generation',
            "PULSE_ONCE_TO_TURN_OFF_OBSERVED_ON_LAMP",
            'cJSON_IsTrue(visual_on)',
            'context->commands_enabled',
        ):
            self.assertIn(marker,s)

    def test_one_pulse_per_visual_check_is_durable(self):
        func=self.cpp.split("std::string process_manual_off_maintenance(",1)[1].split("esp_err_t run_stage_device_runtime(",1)[0]
        pre=func.index("command_replay_remember(request_id)")
        resync=func.index("laser->ResyncOn()")
        output=func.index("laser->SetOff(")
        self.assertLess(pre,resync)
        self.assertLess(resync,output)
        self.assertIn("command_replay_seen(request_id, &seen)",func)
        self.assertIn("relay_pulse_count() == before + 1",func)
        self.assertIn("if (seen)",func)
        self.assertIn("laser->machine().arm_state() != stagelaser::ArmState::kDisarmed",func)
        self.assertNotIn("laser->SetOn(",func)
        self.assertNotIn("laser->Arm(",func)
        self.assertNotIn("command.execute",func)
        self.assertNotIn("target_runtime_snapshot_id",func)

if __name__=="__main__":
    unittest.main()
