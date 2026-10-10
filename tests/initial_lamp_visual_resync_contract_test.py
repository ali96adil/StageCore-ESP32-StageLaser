from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class InitialLampVisualResyncContract(unittest.TestCase):
    def test_visual_off_boot_fenced_initial_assignment_only(self):
        code=(ROOT/"src"/"stage_device_runtime.cpp").read_text()
        self.assertIn('visual_off_resync_allowed',code)
        self.assertIn('visual_off_boot_id',code)
        self.assertIn('request.visual_off_boot_id != boot_id()',code)
        self.assertIn('context->assignment_state != "UNASSIGNED"',code)
        self.assertIn('context.assignment_state == "UNASSIGNED"',code)
        self.assertIn('prepare.visual_off_boot_id == boot_id()',code)
        self.assertIn('laser->machine().logical_state() == stagelaser::LogicalState::kUnknown',code)
        self.assertIn('laser->machine().arm_state() == stagelaser::ArmState::kDisarmed',code)
        self.assertIn('!laser->machine().pulse_in_progress()',code)
        self.assertIn('!laser->machine().flash_active()',code)

    def test_resync_does_not_issue_relay_pulse(self):
        runtime=(ROOT/"src"/"stage_device_runtime.cpp").read_text()
        fragment=runtime.split('if (prepare.visual_off_resync_allowed &&',1)[1].split('err = drive_safe_off(laser);',1)[0]
        self.assertIn('laser->ResyncOff()',fragment)
        self.assertNotIn('relay_pick(',fragment)
        self.assertNotIn('relay_release(',fragment)
        self.assertNotIn('gpio_set_level(',fragment)
        machine=(ROOT/"src"/"laser_state_machine.cpp").read_text()
        resync=machine.split('Decision StateMachine::CommandResyncOff()',1)[1].split('Decision StateMachine::CommandResyncOn()',1)[0]
        self.assertIn('logical_ = LogicalState::kOff;',resync)
        self.assertIn('quality_ = StateQuality::kTracked;',resync)
        self.assertIn('ActuatorAction::kNone',resync)

if __name__=="__main__":
    unittest.main()
