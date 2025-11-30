import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.automation import maybe_simple_id
from esphome.components import uart, sensor
from esphome.const import CONF_ID, CONF_VARIANT

DEPENDENCIES = ["uart"]

CONF_PRESET = "preset"

standing_desk_height_ns = cg.esphome_ns.namespace("standing_desk_height")
StandingDeskHeightSensor = standing_desk_height_ns.class_("StandingDeskHeightSensor", cg.PollingComponent, uart.UARTDevice, sensor.Sensor)

DetectDecoderAction = standing_desk_height_ns.class_("DetectDecoderAction", automation.Action)
MoveUpAction = standing_desk_height_ns.class_("MoveUpAction", automation.Action)
MoveDownAction = standing_desk_height_ns.class_("MoveDownAction", automation.Action)
StopAction = standing_desk_height_ns.class_("StopAction", automation.Action)
PresetAction = standing_desk_height_ns.class_("PresetAction", automation.Action)

SIMPLE_ACTION_SCHEMA = maybe_simple_id(
    {
        cv.Required(CONF_ID): cv.use_id(StandingDeskHeightSensor),
    }
)

PRESET_ACTION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(StandingDeskHeightSensor),
        cv.Required(CONF_PRESET): cv.templatable(cv.int_range(min=1, max=4)),
    }
)

@automation.register_action("standing_desk_height.detect_decoder", DetectDecoderAction, SIMPLE_ACTION_SCHEMA)
async def detect_decoder_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("standing_desk_height.move_up", MoveUpAction, SIMPLE_ACTION_SCHEMA)
async def move_up_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("standing_desk_height.move_down", MoveDownAction, SIMPLE_ACTION_SCHEMA)
async def move_down_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("standing_desk_height.stop", StopAction, SIMPLE_ACTION_SCHEMA)
async def stop_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)

@automation.register_action("standing_desk_height.preset", PresetAction, PRESET_ACTION_SCHEMA)
async def preset_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_PRESET], args, cg.uint8)
    cg.add(var.set_preset(template_))
    return var

DecoderVariants = standing_desk_height_ns.enum("DecoderVariants")
DECODER_VARIANTS = {
    "auto": DecoderVariants.DECODER_VARIANT_UNKNOWN,
    "uplift": DecoderVariants.DECODER_VARIANT_UPLIFT,
    "jarvis": DecoderVariants.DECODER_VARIANT_JARVIS,
    "omnidesk": DecoderVariants.DECODER_VARIANT_OMNIDESK,
    "wn17cm3": DecoderVariants.DECODER_VARIANT_WN17CM3,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        unit_of_measurement="in",
        icon="mdi:human-male-height-variant",
        accuracy_decimals=1
    )
    .extend({
        cv.GenerateID(): cv.declare_id(StandingDeskHeightSensor),
        cv.Optional(CONF_VARIANT): cv.enum(DECODER_VARIANTS, lower=True, space="_"),
    })
    .extend(cv.polling_component_schema("500ms"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    if CONF_VARIANT in config:
        cg.add(var.set_decoder_variant(config[CONF_VARIANT]))

    yield cg.register_component(var, config)
    yield sensor.register_sensor(var, config)
    yield uart.register_uart_device(var, config)

