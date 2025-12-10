import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.const import CONF_ID

CODEOWNERS = ["@scottcurrie"]
DEPENDENCIES = []

# Configuration keys
CONF_PAUSE_THRESHOLD = "pause_threshold"
CONF_RESUME_THRESHOLD = "resume_threshold"
CONF_EMERGENCY_THRESHOLD = "emergency_threshold"
CONF_COMM_TIMEOUT = "comm_timeout"
CONF_NUM_DESKS = "num_desks"
CONF_LOCAL_DESK_SENSOR = "local_desk_sensor"
CONF_CONTROL_LOOP_INTERVAL = "control_loop_interval"

# Namespace
platform_sync_ns = cg.esphome_ns.namespace("platform_sync_controller")
PlatformSyncController = platform_sync_ns.class_("PlatformSyncController", cg.Component)

# Action classes
MoveUpAction = platform_sync_ns.class_("MoveUpAction", automation.Action)
MoveDownAction = platform_sync_ns.class_("MoveDownAction", automation.Action)
StopAction = platform_sync_ns.class_("StopAction", automation.Action)
PresetAction = platform_sync_ns.class_("PresetAction", automation.Action)
MoveToHeightAction = platform_sync_ns.class_("MoveToHeightAction", automation.Action)
EmergencyStopAction = platform_sync_ns.class_("EmergencyStopAction", automation.Action)

# Simple action schema
SIMPLE_ACTION_SCHEMA = maybe_simple_id(
    {
        cv.Required(CONF_ID): cv.use_id(PlatformSyncController),
    }
)

# Preset action schema
PRESET_ACTION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(PlatformSyncController),
        cv.Required("preset"): cv.templatable(cv.int_range(min=1, max=4)),
    }
)

# Move to height action schema
MOVE_TO_HEIGHT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(PlatformSyncController),
        cv.Required("height"): cv.templatable(cv.float_range(min=60.0, max=130.0)),
    }
)

# Register actions
@automation.register_action("platform_sync_controller.move_up", MoveUpAction, SIMPLE_ACTION_SCHEMA)
async def move_up_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("platform_sync_controller.move_down", MoveDownAction, SIMPLE_ACTION_SCHEMA)
async def move_down_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("platform_sync_controller.stop", StopAction, SIMPLE_ACTION_SCHEMA)
async def stop_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("platform_sync_controller.emergency_stop", EmergencyStopAction, SIMPLE_ACTION_SCHEMA)
async def emergency_stop_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("platform_sync_controller.preset", PresetAction, PRESET_ACTION_SCHEMA)
async def preset_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config["preset"], args, cg.uint8)
    cg.add(var.set_preset(template_))
    return var

@automation.register_action("platform_sync_controller.move_to_height", MoveToHeightAction, MOVE_TO_HEIGHT_SCHEMA)
async def move_to_height_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config["height"], args, cg.float_)
    cg.add(var.set_height(template_))
    return var

# Main component schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PlatformSyncController),
        cv.Optional(CONF_PAUSE_THRESHOLD, default=0.3): cv.float_range(min=0.1, max=1.0),
        cv.Optional(CONF_RESUME_THRESHOLD, default=0.2): cv.float_range(min=0.05, max=0.5),
        cv.Optional(CONF_EMERGENCY_THRESHOLD, default=1.0): cv.float_range(min=0.5, max=5.0),
        cv.Optional(CONF_COMM_TIMEOUT, default=250): cv.int_range(min=100, max=5000),
        cv.Optional(CONF_NUM_DESKS, default=5): cv.int_range(min=2, max=10),
        cv.Optional(CONF_CONTROL_LOOP_INTERVAL, default=50): cv.int_range(min=10, max=200),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_pause_threshold(config[CONF_PAUSE_THRESHOLD]))
    cg.add(var.set_resume_threshold(config[CONF_RESUME_THRESHOLD]))
    cg.add(var.set_emergency_threshold(config[CONF_EMERGENCY_THRESHOLD]))
    cg.add(var.set_comm_timeout(config[CONF_COMM_TIMEOUT]))
    cg.add(var.set_num_desks(config[CONF_NUM_DESKS]))
    cg.add(var.set_control_loop_interval(config[CONF_CONTROL_LOOP_INTERVAL]))
