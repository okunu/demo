//
// Created by okunu on 2023/1/25.
//

#ifndef DEMO_SIMPLELIGHTSAMPLE_H
#define DEMO_SIMPLELIGHTSAMPLE_H

#include "BaseSample.h"
#include "glm-0.9.9.8/glm/glm.hpp"
#include "glm-0.9.9.8/glm/gtx/transform.hpp"
#include "glm-0.9.9.8/glm/gtc/type_ptr.hpp"
#include "Shader_m.h"
#include "Camera_m.h"

class SimpleLightSample: public BaseSample{
    using Super = BaseSample;
public:
    SimpleLightSample();
    ~SimpleLightSample();

    void init() override;
    void draw() override;
    void prepareData() override;

private:
    GLuint secProgramObj_;
    GLuint secVertexShader_;
    GLuint secFragmentShader_;
    GLuint vao_;
    GLuint secVao_;
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;

    //光源shader
    Shader lightShader;
    //被照射物体shader
    Shader objectShader;
    Camera camera;
};

#endif //DEMO_SIMPLELIGHTSAMPLE_H
