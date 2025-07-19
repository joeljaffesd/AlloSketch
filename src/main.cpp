// Joel A. Jaffe 2025-07-19
// Updated MAT-201B/Allorenz_JN/Allorenz_JN.cpp 
// to work distributed and with real-time audio input

// Single macro to switch between desktop and Allosphere configurations
#define DESKTOP

#ifdef DESKTOP
  // Desktop configuration
  #define SAMPLE_RATE 48000
  #define AUDIO_CONFIG SAMPLE_RATE, 128, 2, 8
  #define SPATIALIZER_TYPE al::AmbisonicsSpatializer
  #define SPEAKER_LAYOUT al::StereoSpeakerLayout()
#else
  // Allosphere configuration
  #define SAMPLE_RATE 44100
  #define AUDIO_CONFIG SAMPLE_RATE, 256, 60, 9
  #define SPATIALIZER_TYPE al::Dbap
  #define SPEAKER_LAYOUT al::AlloSphereSpeakerLayoutCompensated()
#endif

#include <cstdio>  // for printing to stdout
#include "al/app/al_App.hpp"
#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/io/al_AudioIO.hpp"
#include "al/math/al_Random.hpp"
#include "al/scene/al_DistributedScene.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"
using namespace al;

class Attractor : public PositionedVoice {
private:
  static const int P = 15, D = 10;
  Parameter p[P]{
    {"N", "p", 10000, 0, 20000},    // p[0] = N     | (simulation steps)
    {"h", "p", 0.01, 0, 0.018},     // p[1] = h     | (simulation time step)
    {"x0", "p", 0, -D, D},          // p[2] = x0    | initial
    {"y0", "p", 0.1, -D, D},        // p[3] = y0    | conditions
    {"z0", "p", 0, -D, D},          // p[4] = z0    |
    {"rho", "p", 28, 0, 56},        // p[5] = rho   | simulation
    {"sigma", "p", 10, 0, 20},      // p[6] = sigma | parameters
    {"beta", "p", 8.0f / 3, 0, 4},  // p[7] = beta  |
    {"a", "p", 5, -D, 60},          // p[8] = a     |
    {"b", "p", -10, -D, D},         // p[9] = b     |
    {"c", "p", -10, -D, D},         // p[10] = c    |
    {"d", "p", -10, -D, D},         // p[11] = d    |
    {"e", "p", -10, -D, D},         // p[12] = e    |
    {"o", "p", -10, -D, D},         // p[13] = o    |
    {"g", "p", -10, -D, D},         // p[14] = g    |
  };

  Parameter width {"width", "", 0.07, 0, 0.2};
  Parameter gain {"gain", "", -90, -90, 0};
  ParameterBool light {"light", "", false};  // switch light
  ParameterInt mode {"mode", "", 0, 0, 3};
  Mesh system, point;

public:

  void init() override {
    for (int i = 0; i < P; i++) {
      this->registerParameter(p[i]);
    }
    this->registerParameters(width, gain, light, mode);
  }

  void setMode(int desiredMode) {
    this->mode = desiredMode;
  }

  void toggleLight() {
    this->light = !this->light;
  }

  void update(double dt) override {
    system.reset();
    system.primitive(Mesh::LINE_STRIP);  // defines the nature of the drawn
    system.vertex(p[2], p[3], p[4]);

    for (int i = 0; i < (int)p[0]; i++) {  // draw a point for each iteration
      Vec3f _(system.vertices().back());

      if (mode == 0) {
        // Allotsucs, based on the Den Tsucs Attractor by Paul Bourke
        float a(p[8]), c(p[10]), e(p[12]), o(p[14]);
        float h(p[1]);
        Vec3f f((a * (_.y - _.x)) + (_.x * _.z),  //
                (o * _.y) - (_.x * _.z),          //
                (c * _.z) + (_.x * _.y) - (e * pow(_.x, 2)));
        system.vertex(_ + h * f);
        // the line above is Euler's method! */
      }

      if (mode == 1) {
        // Lorenz
        float rho(p[5]), sigma(p[6]), beta(p[7]), h(p[1]);
        Vec3f f(sigma * (_.y - _.x),      //
                _.x * (rho - _.z) - _.y,  //
                _.x * _.y - beta * _.z);
        system.vertex(_ + h * f);
        // the line above is Euler's method!
      }

      if (mode == 2) {
        // Allorenz
        float rho(p[5]), sigma(p[6]), beta(p[7]), h(p[1]);  // a, b, c
        Vec3f f(_.y * _.z,          // main equation
                rho * (_.x - _.y),  // main equation
                sigma - beta * _.x * _.y - (1 - beta) * pow(_.x, 2));  // main
        system.vertex(_ + h * f);
        // the line above is Euler's method!
      }

      if (mode == 3) {
        // Allorenz. Based on the Chen - Lee Attractor
        float a(p[8]), b(p[9]), d(p[11]), h(p[1]);  // a, c, d, e, f
        Vec3f f((a * _.x) - (_.y * _.z), (_.y * b) + (_.x * _.z),
                (d * _.z) + (_.x * _.y / 3));

        system.vertex(_ + h * f);
        // the line above is Euler's method!
      }
    }

    system.ribbonize(width, true);
    system.primitive(Mesh::TRIANGLE_STRIP);
    system.generateNormals();
  }

  void onProcess(Graphics& g) override {
    g.depthTesting(light);
    g.lighting(light);
    g.blendTrans();
    g.color(1);
    g.scale(0.1);
    g.draw(system);
  }
};

struct MyApp : public DistributedApp {  // use simple app if not distributed
  PresetHandler presetHandler{"presets"};
  DistributedScene scene;
  Attractor* mAttractor = nullptr;

  void onInit() override {
    scene.registerSynthClass<Attractor>();
    scene.verbose(true);
    this->registerDynamicScene(scene);
  }

  void onCreate() override {
    nav().pos(0, 0, 10);
  }

  // TODO
  void onSound(AudioIOData& io) override {}

  void onAnimate(double dt) override { 
    scene.update(dt); 
  }

  void onDraw(Graphics& g) override {
    g.clear(0);

    // draw system if it exists
    scene.render(g);
  }

  bool onKeyDown(const Keyboard& k) override {
    if (isPrimary()) {
      if (!mAttractor) {
        if (k.key() == ' ') {
          std::cout << "Making an attractor!" << std::endl;
          mAttractor = scene.getVoice<Attractor>();
          scene.triggerOn(mAttractor);
          auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
          auto& gui = GUIdomain->newGUI();
          gui.add(presetHandler);

          auto params = mAttractor->parameters();
          for (auto& param : params) {
            gui.add(*param);
            presetHandler << *param;
          }

          presetHandler.recallPresetSynchronous(7);  // initial condition on startup, how to make autocue?
          std::cout << "Finished making attractor!" << std::endl;
        }
      } else {
        if (k.key() == 'l') {
          mAttractor->toggleLight();
        }
        else if (isPrimary() && k.key() == '0') {
          mAttractor->setMode(0);
        }
        else if (isPrimary() && k.key() == '1') {
          mAttractor->setMode(1);
        }
        else if (isPrimary() && k.key() == '2') {
          mAttractor->setMode(2);
        }
        else if (isPrimary() && k.key() == '3') {
          mAttractor->setMode(3);
        }
      }
    }
    return true;
  }

};

int main() {
  MyApp app;
  app.configureAudio(AUDIO_CONFIG);
  app.start();
}