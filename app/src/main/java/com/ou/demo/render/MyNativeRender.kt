package com.ou.demo.render

import android.view.Surface

class MyNativeRender {

    external fun native_init(type: Int)
    external fun native_uninit()
    external fun native_onSurfaceCreate(surface: Surface)
    external fun native_onSurfaceChanged(width: Int, heiht: Int)
    external fun native_onDrawFrame()
    external fun changeDirection(direction: Int)
    external fun rorate(offsetX: Float, offsetY: Float, distance: Float)
    external fun scale(d: Float)
}