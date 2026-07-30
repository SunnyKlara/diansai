# 待 K230 真机验证：本次仅整理交付包，未在 K230 上运行。

"""K230 fixed-pipe 416x160 ROI detector, position output and UART V1.

First run:
    Put the ball against the LEFT inner end, then against the RIGHT inner end.
    The program captures both end-centre positions and saves calibration.
    No printed scale marks are required. A 1 cm ball in a 25 cm pipe has a
    centre travel of 24 cm, so the two end centres are -12 cm and +12 cm.

Normal run:
    - Crop only the fixed pipe from the 640x480 camera image.
    - Add the same vertical padding used to build the training dataset.
    - Run the rectangular 416x160 Kmodel and restore coordinates to 640x480.
    - Project the ball center onto the calibrated pipe axis.
    - Apply a 3-sample median filter followed by a fast exponential filter.
    - Display the signed position in centimetres.
    - Send one filtered position after every inference on UART2
      (IO11 TX, IO12 RX), 115200 8N1.

UART frame:
    $BP,1,+5.23*CS\r\n   valid measurement
    $BP,0,0.00*CS\r\n    ball unavailable
CS is the XOR of characters between '$' and '*'.
"""

from libs.PipeLine import PipeLine
from libs.YOLO import YOLOv8
from libs.Utils import ScopedTiming
from media.sensor import Sensor
from machine import UART, FPIOA
import nncase_runtime as nn
import ujson
import gc
import os
import time


BASE_DIR = "/sdcard/steelball/fixed_pipe_roi416x160_v1"
KMODEL_PATH = BASE_DIR + "/fixed_pipe_ball_roi416x160_v1.kmodel"
CALIBRATION_PATH = BASE_DIR + "/position_calibration.json"
LEGACY_CALIBRATION_PATH = (
    "/sdcard/steelball/fixed_pipe_480_v1/position_calibration.json"
)

LABELS = ["ball_in_fixed_pipe"]
MODEL_INPUT_SIZE = [416, 160]
CAMERA_AI_SIZE = [640, 480]

# Fixed-camera ROI in the 640x480 camera coordinate system. The 608x160 crop
# is padded to 608x234 (37 pixels above and below), then resized isotropically
# to 416x160. This exactly matches the protected training dataset.
ROI_X = 16
ROI_Y = 208
ROI_WIDTH = 608
ROI_HEIGHT = 160
ROI_PAD_TOP = 37
ROI_PAD_BOTTOM = 37
ROI_PADDED_SIZE = [608, 234]

SENSOR_ID = 2
SENSOR_WIDTH = 1920
SENSOR_HEIGHT = 1080
SENSOR_FPS = 60
CONFIDENCE_THRESHOLD = 0.50
NMS_THRESHOLD = 0.40
MAX_BOXES = 1

# Two-point calibration uses the two mechanical ends, not printed scale marks.
# Pipe 25 cm, ball diameter 1 cm -> ball-centre travel = 24 cm.
CAL_NEGATIVE_CM = -12.0
CAL_POSITIVE_CM = 12.0
CAL_SETTLE_SECONDS = 8
CAL_SAMPLE_SECONDS = 3
CAL_MIN_SAMPLES = 15

# Filter: a short median window removes isolated jumps while keeping the delay
# low enough for the balance controller; EMA smooths the remaining pixel noise.
MEDIAN_WINDOW = 3
EMA_GAIN = 0.70
ZERO_SNAP_CM = 0.05
MISS_HOLD_FRAMES = 1
FILTER_RESET_MISSES = 12

# The physical pipe coordinate range is -12.5 cm to +12.5 cm.
POSITION_MIN_CM = -12.0
POSITION_MAX_CM = 12.0

# External serial port: LC K230 IO11=UART2_TXD, IO12=UART2_RXD.
UART_ENABLE = True
UART_ID = 2
UART_TX_PIN = 11
UART_RX_PIN = 12
UART_BAUD = 115200

# Send the newest filtered result immediately after every inference. At the
# measured 480-model speed this is about 24~25 packets per second.
UART_SEND_EVERY_FRAMES = 1

PRINT_EVERY = 50
# Keep True while verifying the boxes and position. Set False for the lowest
# latency competition mode; inference and UART continue without the preview.
PREVIEW_ENABLE = True
PREVIEW_EVERY_FRAMES = 10
GC_EVERY_FRAMES = 25

# Keep False in long-running/controller mode. Change to True only for a short
# CanMV IDE box check; USB video streaming costs FPS and may destabilise USBDBG.
IDE_STREAM_ENABLE = True


class FixedPipeRoiYOLOv8(YOLOv8):
    """YOLOv8 with fixed crop+pad+resize preprocessing."""

    def config_preprocess(self, input_image_size=None):
        with ScopedTiming("set ROI preprocess config", self.debug_mode > 0):
            source_size = input_image_size if input_image_size else CAMERA_AI_SIZE
            self.ai2d.crop(ROI_X, ROI_Y, ROI_WIDTH, ROI_HEIGHT)
            self.ai2d.pad(
                [0, 0, 0, 0, ROI_PAD_TOP, ROI_PAD_BOTTOM, 0, 0],
                0,
                [128, 128, 128],
            )
            self.ai2d.resize(
                nn.interp_method.tf_bilinear,
                nn.interp_mode.half_pixel,
            )
            self.ai2d.build(
                [1, 3, source_size[1], source_size[0]],
                [1, 3, self.model_input_size[1], self.model_input_size[0]],
            )


def median(values):
    ordered = sorted(values)
    count = len(ordered)
    middle = count // 2
    if count & 1:
        return float(ordered[middle])
    return (ordered[middle - 1] + ordered[middle]) / 2.0


class PositionFilter:
    def __init__(self):
        self.window = []
        self.value = None

    def reset(self):
        self.window = []
        self.value = None

    def update(self, raw_value):
        self.window.append(float(raw_value))
        if len(self.window) > MEDIAN_WINDOW:
            self.window.pop(0)
        middle = median(self.window)
        if self.value is None:
            self.value = middle
        else:
            self.value += EMA_GAIN * (middle - self.value)
        return self.value


def heap_free_kb():
    try:
        return gc.mem_free() // 1024
    except Exception:
        return -1


def open_uart():
    if not UART_ENABLE:
        return None
    try:
        fpioa = FPIOA()
        fpioa.set_function(UART_TX_PIN, FPIOA.UART2_TXD)
        fpioa.set_function(UART_RX_PIN, FPIOA.UART2_RXD)
        uart = UART(UART_ID, UART_BAUD)
        print(
            "POSITION_UART_READY uart={} tx=IO{} rx=IO{} baud={}".format(
                UART_ID, UART_TX_PIN, UART_RX_PIN, UART_BAUD
            )
        )
        return uart
    except Exception as exc:
        print("POSITION_UART_INIT_FAILED", exc)
        return None


def uart_checksum(body):
    checksum = 0
    for character in body:
        checksum ^= ord(character)
    return checksum


def build_uart_frame(valid, position_cm):
    if valid:
        body = "BP,1,{:+.2f}".format(position_cm)
    else:
        body = "BP,0,0.00"
    return "${}*{:02X}\r\n".format(body, uart_checksum(body))


def get_primary_detection(result, display_scale_x, display_scale_y):
    if not result or len(result[0]) == 0:
        return None

    best_index = 0
    for index in range(1, len(result[0])):
        if result[2][index] > result[2][best_index]:
            best_index = index

    x, y, width, height = result[0][best_index]
    display_center_x = x + width / 2.0
    display_center_y = y + height / 2.0
    # YOLO postprocess returns coordinates in the 608x234 padded ROI canvas.
    # Remove the artificial top pad and restore the full 640x480 origin.
    camera_center_x = ROI_X + display_center_x
    camera_center_y = ROI_Y + display_center_y - ROI_PAD_TOP
    camera_box_x = ROI_X + x
    camera_box_y = ROI_Y + y - ROI_PAD_TOP
    return (
        camera_center_x,
        camera_center_y,
        camera_center_x * display_scale_x,
        camera_center_y * display_scale_y,
        float(result[2][best_index]),
        camera_box_x * display_scale_x,
        camera_box_y * display_scale_y,
        width * display_scale_x,
        height * display_scale_y,
    )


def draw_detection(osd, detection):
    if detection is None:
        return
    x = int(detection[5])
    y = int(detection[6])
    width = int(detection[7])
    height = int(detection[8])
    center_x = int(detection[2])
    center_y = int(detection[3])
    osd.draw_rectangle(x, y, width, height, color=(0, 255, 0), thickness=3)
    osd.draw_line(
        center_x - 8, center_y, center_x + 8, center_y, color=(255, 0, 0)
    )
    osd.draw_line(
        center_x, center_y - 8, center_x, center_y + 8, color=(255, 0, 0)
    )


def draw_roi(osd, display_scale_x, display_scale_y):
    osd.draw_rectangle(
        int(ROI_X * display_scale_x),
        int(ROI_Y * display_scale_y),
        int(ROI_WIDTH * display_scale_x),
        int(ROI_HEIGHT * display_scale_y),
        color=(255, 255, 0),
        thickness=2,
    )


def save_calibration(negative_point, positive_point):
    data = {
        "version": 2,
        "method": "1cm_ball_centres_at_25cm_pipe_ends",
        "negative_cm": CAL_NEGATIVE_CM,
        "negative_point": [negative_point[0], negative_point[1]],
        "positive_cm": CAL_POSITIVE_CM,
        "positive_point": [positive_point[0], positive_point[1]],
        "camera_size": CAMERA_AI_SIZE,
    }
    with open(CALIBRATION_PATH, "w") as calibration_file:
        calibration_file.write(ujson.dumps(data))
    print("CALIBRATION_SAVED", CALIBRATION_PATH, data)


def load_calibration():
    last_error = None
    for path in (CALIBRATION_PATH, LEGACY_CALIBRATION_PATH):
        try:
            with open(path, "r") as calibration_file:
                data = ujson.loads(calibration_file.read())
            negative_point = data["negative_point"]
            positive_point = data["positive_point"]
            dx = float(positive_point[0]) - float(negative_point[0])
            dy = float(positive_point[1]) - float(negative_point[1])
            if dx * dx + dy * dy < 100.0:
                raise ValueError("calibration points are too close")
            print("CALIBRATION_LOADED path={} data={}".format(path, data))
            return (
                (float(negative_point[0]), float(negative_point[1])),
                (float(positive_point[0]), float(positive_point[1])),
            )
        except Exception as exc:
            last_error = exc
    print("CALIBRATION_REQUIRED", last_error)
    return None


def capture_calibration_point(
    pipeline, detector, scale_x, scale_y, label, known_cm
):
    samples_x = []
    samples_y = []
    started = time.ticks_ms()
    settle_ms = CAL_SETTLE_SECONDS * 1000
    total_ms = (CAL_SETTLE_SECONDS + CAL_SAMPLE_SECONDS) * 1000

    print(
        "CALIBRATION_PLACE_BALL label={} position_cm={:+.1f}".format(
            label, known_cm
        )
    )

    while True:
        elapsed = time.ticks_diff(time.ticks_ms(), started)
        if elapsed >= total_ms:
            break

        frame = pipeline.get_frame()
        result = detector.run(frame)
        detection = get_primary_detection(result, scale_x, scale_y)
        osd = pipeline.osd_img
        osd.clear()
        draw_detection(osd, detection)

        if elapsed < settle_ms:
            remaining = (settle_ms - elapsed + 999) // 1000
            message = "PUT BALL AT {}  {}s".format(label, remaining)
            color = (255, 255, 0)
        else:
            if detection is not None:
                samples_x.append(detection[0])
                samples_y.append(detection[1])
            remaining = (total_ms - elapsed + 999) // 1000
            message = "HOLD {}  sampling {}s".format(label, remaining)
            color = (0, 255, 255)

        osd.draw_string_advanced(5, 5, 24, message, color=color)
        osd.draw_string_advanced(
            5,
            36,
            22,
            "samples:{}".format(len(samples_x)),
            color=(255, 255, 255),
        )
        pipeline.show_image()
        gc.collect()

    if len(samples_x) < CAL_MIN_SAMPLES:
        raise RuntimeError(
            "{} calibration failed: only {} samples".format(label, len(samples_x))
        )

    point = (median(samples_x), median(samples_y))
    print(
        "CALIBRATION_POINT label={} cm={:+.1f} x={:.2f} y={:.2f} samples={}".format(
            label, known_cm, point[0], point[1], len(samples_x)
        )
    )
    return point


def perform_calibration(pipeline, detector, scale_x, scale_y):
    negative_point = capture_calibration_point(
        pipeline,
        detector,
        scale_x,
        scale_y,
        "LEFT END",
        CAL_NEGATIVE_CM,
    )
    positive_point = capture_calibration_point(
        pipeline,
        detector,
        scale_x,
        scale_y,
        "RIGHT END",
        CAL_POSITIVE_CM,
    )

    dx = positive_point[0] - negative_point[0]
    dy = positive_point[1] - negative_point[1]
    if dx * dx + dy * dy < 100.0:
        raise RuntimeError("calibration points are too close; repeat calibration")
    save_calibration(negative_point, positive_point)
    return negative_point, positive_point


def position_from_point(ball_point, negative_point, positive_point):
    axis_x = positive_point[0] - negative_point[0]
    axis_y = positive_point[1] - negative_point[1]
    denominator = axis_x * axis_x + axis_y * axis_y
    projection = (
        (ball_point[0] - negative_point[0]) * axis_x
        + (ball_point[1] - negative_point[1]) * axis_y
    ) / denominator
    return CAL_NEGATIVE_CM + projection * (
        CAL_POSITIVE_CM - CAL_NEGATIVE_CM
    )


def camera_to_display(point, inv_scale_x, inv_scale_y):
    return int(point[0] * inv_scale_x), int(point[1] * inv_scale_y)


def draw_calibrated_axis(
    osd, negative_point, positive_point, inv_scale_x, inv_scale_y
):
    # Ball-centre end points span 24 cm. Extend each side by the 0.5 cm radius
    # to draw the complete 25 cm physical pipe.
    dx = positive_point[0] - negative_point[0]
    dy = positive_point[1] - negative_point[1]
    radius_extension = 0.5 / 24.0
    left_end = (
        negative_point[0] - radius_extension * dx,
        negative_point[1] - radius_extension * dy,
    )
    right_end = (
        positive_point[0] + radius_extension * dx,
        positive_point[1] + radius_extension * dy,
    )
    center = (
        (negative_point[0] + positive_point[0]) / 2.0,
        (negative_point[1] + positive_point[1]) / 2.0,
    )

    left_x, left_y = camera_to_display(left_end, inv_scale_x, inv_scale_y)
    right_x, right_y = camera_to_display(right_end, inv_scale_x, inv_scale_y)
    center_x, center_y = camera_to_display(center, inv_scale_x, inv_scale_y)
    osd.draw_line(left_x, left_y, right_x, right_y, color=(0, 128, 255), thickness=2)
    osd.draw_line(
        center_x - 8, center_y, center_x + 8, center_y, color=(255, 255, 0)
    )
    osd.draw_line(
        center_x, center_y - 8, center_x, center_y + 8, color=(255, 255, 0)
    )


def run(max_frames=0):
    pipeline = None
    detector = None
    uart = None
    high_fps_sensor = None
    position_filter = PositionFilter()
    frame_count = 0
    uart_tx_count = 0
    report_uart_tx_start = 0
    missed_frames = 0
    last_position = 0.0
    report_start_ms = time.ticks_ms()
    fps = 0.0
    uart_hz = 0.0
    cached_uart_key = None
    cached_uart_packet = None
    max_uart_write_ms = 0

    try:
        uart = open_uart()
        # PipeLine's bundled board map forces this board to 30 FPS when it
        # creates the Sensor itself. Supplying an explicit GC2093 Sensor keeps
        # the same 1920x1080 geometry while requesting the supported 60 FPS
        # mode, so the existing calibration remains geometrically compatible.
        high_fps_sensor = Sensor(
            id=SENSOR_ID,
            width=SENSOR_WIDTH,
            height=SENSOR_HEIGHT,
            fps=SENSOR_FPS,
        )
        pipeline = PipeLine(
            rgb888p_size=CAMERA_AI_SIZE,
            display_mode="lcd",
            display_size=None,
        )
        # IDE_STREAM_ENABLE=False is essential for long-running control:
        # PREVIEW_ENABLE only controls OSD updates, while the PipeLine video
        # layer can otherwise keep streaming camera frames over USBDBG.
        pipeline.create(
            sensor=high_fps_sensor,
            fps=SENSOR_FPS,
            to_ide=IDE_STREAM_ENABLE,
        )
        display_size = pipeline.get_display_size()

        detector = FixedPipeRoiYOLOv8(
            task_type="detect",
            mode="video",
            kmodel_path=KMODEL_PATH,
            labels=LABELS,
            # Make the inherited postprocess decode into the padded ROI canvas.
            rgb888p_size=ROI_PADDED_SIZE,
            model_input_size=MODEL_INPUT_SIZE,
            display_size=ROI_PADDED_SIZE,
            conf_thresh=CONFIDENCE_THRESHOLD,
            nms_thresh=NMS_THRESHOLD,
            max_boxes_num=MAX_BOXES,
            debug_mode=0,
        )
        detector.config_preprocess(input_image_size=CAMERA_AI_SIZE)

        display_scale_x = display_size[0] / float(CAMERA_AI_SIZE[0])
        display_scale_y = display_size[1] / float(CAMERA_AI_SIZE[1])

        calibration = load_calibration()
        if calibration is None:
            calibration = perform_calibration(
                pipeline, detector, display_scale_x, display_scale_y
            )
        negative_point, positive_point = calibration

        print(
            "PIPE_BALL_ROI416X160_V1_READY sensor={}x{}@{} roi=({},{} {}x{}) input={}x{} conf={} uart={} baud={} filter=median{}+ema{} to_ide={} preview={} preview_div={}".format(
                SENSOR_WIDTH,
                SENSOR_HEIGHT,
                SENSOR_FPS,
                ROI_X,
                ROI_Y,
                ROI_WIDTH,
                ROI_HEIGHT,
                MODEL_INPUT_SIZE[0],
                MODEL_INPUT_SIZE[1],
                CONFIDENCE_THRESHOLD,
                UART_ID,
                UART_BAUD,
                MEDIAN_WINDOW,
                EMA_GAIN,
                1 if IDE_STREAM_ENABLE else 0,
                1 if PREVIEW_ENABLE else 0,
                PREVIEW_EVERY_FRAMES,
            )
        )

        while max_frames == 0 or frame_count < max_frames:
            # Required by the official CanMV long-running examples. This gives
            # the runtime a safe point for IDE control and system housekeeping.
            os.exitpoint()
            with ScopedTiming("total", False):
                frame = pipeline.get_frame()
                result = detector.run(frame)
                detection = get_primary_detection(
                    result,
                    display_scale_x,
                    display_scale_y,
                )

                if detection is not None:
                    raw_position = position_from_point(
                        (detection[0], detection[1]),
                        negative_point,
                        positive_point,
                    )
                    if raw_position < POSITION_MIN_CM:
                        raw_position = POSITION_MIN_CM
                    elif raw_position > POSITION_MAX_CM:
                        raw_position = POSITION_MAX_CM
                    last_position = position_filter.update(raw_position)
                    missed_frames = 0
                else:
                    raw_position = None
                    missed_frames += 1
                    if missed_frames >= FILTER_RESET_MISSES:
                        position_filter.reset()

                valid = (
                    position_filter.value is not None
                    and missed_frames <= MISS_HOLD_FRAMES
                )
                display_position = last_position
                if abs(display_position) < ZERO_SNAP_CM:
                    display_position = 0.0

                frame_count += 1
                now_ms = time.ticks_ms()

                # A short UART packet at 115200 baud takes less than 2 ms.
                # Sending every inference avoids the old 50 ms timer being
                # quantised to two frames (about 12 Hz at 24~25 FPS).
                if (
                    uart is not None
                    and frame_count % UART_SEND_EVERY_FRAMES == 0
                ):
                    try:
                        # Reuse the same byte buffer while the rounded position
                        # is unchanged. This greatly reduces long-run heap
                        # fragmentation from formatting a new string each frame.
                        if valid:
                            rounded_position = (
                                int(
                                    display_position * 100.0
                                    + (0.5 if display_position >= 0.0 else -0.5)
                                )
                            )
                            uart_key = (1, rounded_position)
                        else:
                            uart_key = (0, 0)
                        if uart_key != cached_uart_key:
                            packet_position = uart_key[1] / 100.0
                            cached_uart_packet = build_uart_frame(
                                uart_key[0] == 1,
                                packet_position,
                            ).encode()
                            cached_uart_key = uart_key

                        uart_write_start = time.ticks_ms()
                        written = uart.write(cached_uart_packet)
                        uart_write_ms = time.ticks_diff(
                            time.ticks_ms(),
                            uart_write_start,
                        )
                        if uart_write_ms > max_uart_write_ms:
                            max_uart_write_ms = uart_write_ms
                        if (
                            written is not None
                            and written != len(cached_uart_packet)
                        ):
                            print(
                                "POSITION_UART_SHORT_WRITE expected={} actual={}".format(
                                    len(cached_uart_packet),
                                    written,
                                )
                            )
                        uart_tx_count += 1
                    except Exception as exc:
                        print("POSITION_UART_WRITE_FAILED", exc)
                        try:
                            uart.deinit()
                        except Exception:
                            pass
                        uart = None

                if frame_count % PRINT_EVERY == 0:
                    elapsed_ms = time.ticks_diff(now_ms, report_start_ms)
                    if elapsed_ms > 0:
                        fps = PRINT_EVERY * 1000.0 / elapsed_ms
                        uart_hz = (
                            (uart_tx_count - report_uart_tx_start)
                            * 1000.0
                            / elapsed_ms
                        )
                    report_start_ms = now_ms
                    report_uart_tx_start = uart_tx_count
                    if valid:
                        print(
                            "BALL_ROI_V1 valid=1 raw={:+.3f} filtered={:+.3f} fps={:.2f} uart_hz={:.2f} tx={} heap_kb={} uart_ms_max={}".format(
                                raw_position if raw_position is not None else last_position,
                                display_position,
                                fps,
                                uart_hz,
                                uart_tx_count,
                                heap_free_kb(),
                                max_uart_write_ms,
                            )
                        )
                    else:
                        print(
                            "BALL_ROI_V1 valid=0 filtered=NA fps={:.2f} uart_hz={:.2f} tx={} heap_kb={} uart_ms_max={}".format(
                                fps,
                                uart_hz,
                                uart_tx_count,
                                heap_free_kb(),
                                max_uart_write_ms,
                            )
                        )
                    max_uart_write_ms = 0

                if valid:
                    position_text = "POS:{:+.2f} cm".format(display_position)
                    position_color = (0, 255, 0)
                else:
                    position_text = "POS: NO BALL"
                    position_color = (255, 0, 0)

                preview_due = (
                    PREVIEW_ENABLE
                    and frame_count % PREVIEW_EVERY_FRAMES == 0
                )
                if preview_due:
                    osd = pipeline.osd_img
                    osd.clear()
                    draw_roi(osd, display_scale_x, display_scale_y)
                    draw_calibrated_axis(
                        osd,
                        negative_point,
                        positive_point,
                        display_scale_x,
                        display_scale_y,
                    )
                    draw_detection(osd, detection)
                    osd.draw_string_advanced(
                        5, 5, 30, position_text, color=position_color
                    )
                    osd.draw_string_advanced(
                        5,
                        42,
                        22,
                        "UART2 TX {:.1f}Hz  AI {:.1f}FPS".format(
                            uart_hz,
                            fps,
                        ),
                        color=(255, 255, 0),
                    )
                    pipeline.show_image()

                if frame_count % GC_EVERY_FRAMES == 0:
                    frame = None
                    result = None
                    detection = None
                    gc.collect()

        print(
            "PIPE_BALL_ROI416X160_V1_TEST_OK frames={} tx={}".format(
                frame_count,
                uart_tx_count,
            )
        )
    finally:
        if uart is not None:
            try:
                uart.deinit()
            except Exception:
                pass
        if detector is not None:
            detector.deinit()
        if pipeline is not None:
            pipeline.destroy()
        gc.collect()
        print("PIPE_BALL_ROI416X160_V1_STOPPED")


if __name__ == "__main__":
    run()
