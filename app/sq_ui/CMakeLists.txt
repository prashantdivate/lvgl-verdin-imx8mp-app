SET(SOURCES components/ui_comp_alarm_comp.c
    components/ui_comp_clock_dot.c
    components/ui_comp_scrolldots.c
    components/ui_comp_small_label.c
    components/ui_comp.c
    screens/ui_Splash.c
    screens/ui_Clock.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_mad_logo_png.c
    fonts/ui_font_Number.c)

add_library(ui ${SOURCES})
