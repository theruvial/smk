kb_dir = meson.current_source_dir()

# Newmen GM610 (PCB "K948 US BYK916 V1.1").
# Bring-up order: typing (done) -> per-key RGB -> BK3632 wireless -> live keymap.
# Enable a stage by uncommenting its option below.
keyboards += {
    'newmen-gm610' : {
        'dir' : kb_dir,
        'platform' : 'sh68f90',
        'icp_part' : 'sh68f90a',
        'src' : files(
            'user_matrix.c',
            'user_init.c',
            'user_sleep.c',
            'kb.c',
        ),
        'layouts' : {
            'default' : {
                'dir' : kb_dir / 'layouts/default',
                'src' : files('layouts/default/layout.c', 'layouts/default/indicators.c'),
            },
        },
        'options' : {
            # Stock USB IDs: SMK serves the ISP report on the same interface (1) and
            # report ID (5) as the stock app, so one sinowisp command reflashes either.
            'vendor_id' : '0x12c9',
            'product_id' : '0x6001',
            # per-key RGB: 5 x 14 LED matrix, effects centred between H and J
            'led_rows' : '5',
            'led_cols' : '14',
            'led_cx2' : '13',
            'led_cy2' : '4',
            # 150 us LED subframes (core default 400), 6 per key scan (kbdef.h):
            # ~300 Hz refresh. Shorter would hand most of the CPU to the LED ISR.
            'led_subframe_us' : '150', # needs USB at high IRQ priority (user_init.c)
            # BK3632 Bluetooth / 2.4 GHz (stage 3)
            'wireless' : 'bk3632',
            # Keymap editable live from tools/configurator (stage 4)
            # 'dynamic_keymap_layers' : '4',
        },
    },
}
