/*
Allocore Example: Networked Texture

Description:
This demonstrates how to distribute texture data across nodes using POD state.
The primary node manipulates texture data and pushes it to the state, while
secondary nodes read the texture from state and display it.

Joel A. Jaffe, July 2025

Modified from Frame Feedback example by:
Lance Putnam, Nov. 2014
Keehong Youn, 2017
*/

#include <iostream>
#include <cmath>
#include "al/app/al_DistributedApp.hpp"
#include "al/graphics/al_Texture.hpp"
#include "al_ext/statedistribution/al_CuttleboneDomain.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
using namespace al;

#ifndef RESOLUTION
#define RESOLUTION 1080
#endif

// Define texture dimensions (must be reasonable size for network transmission)
constexpr int TEX_WIDTH = RESOLUTION;
constexpr int TEX_HEIGHT = RESOLUTION;
constexpr int TEX_SIZE = TEX_WIDTH * TEX_HEIGHT * 3; // RGB format

// POD state struct for networked texture data
struct TextureState {
  float time;                               // Animation time
  float angle;                              // Rotation angle
  unsigned char textureData[TEX_SIZE];      // Raw texture data (RGB)
  bool textureNeedsUpdate;                  // Flag to signal texture update
  int frameCounter;                         // Frame counter for debugging
};

struct MyApp : public DistributedAppWithState<TextureState> {
  Mesh shape;
  Texture texBlur;
  Texture distributedTexture;  // Texture for displaying networked data

  void onInit() override {
    auto cuttleboneDomain =
      CuttleboneStateSimulationDomain<TextureState>::enableCuttlebone(this);
    if (!cuttleboneDomain) {
      std::cerr << "ERROR: Could not start Cuttlebone. Quitting." << std::endl;
      quit();
    }    
  }

  void onCreate() override {
    // Debug output
    std::cout << "Node started as: " << (isPrimary() ? "PRIMARY" : "SECONDARY") << std::endl;
    
    // Create a colored square
    shape.primitive(Mesh::LINE_LOOP);
    const int N = 4;
    for (int i = 0; i < N; ++i) {
      float theta = float(i) / N * 2 * M_PI;
      shape.vertex(cos(theta), sin(theta));
      shape.color(HSV(theta / 2 / M_PI));
    }

    texBlur.filter(Texture::LINEAR);
    
    // Initialize distributed texture
    distributedTexture.create2D(TEX_WIDTH, TEX_HEIGHT, Texture::RGB8);
    distributedTexture.filter(Texture::LINEAR);
    
    // Initialize state (only on primary)
    if (isPrimary()) {
      state().time = 0.0f;
      state().angle = 0.0f;
      state().textureNeedsUpdate = false;
      state().frameCounter = 0;
      generateTextureData();
      std::cout << "Primary: Initial texture generated" << std::endl;
    }
  }

  void generateTextureData() {
    // Generate procedural texture data based on current state
    float t = state().time;
    float rotation = state().angle * M_PI / 180.0f;
    
    for (int y = 0; y < TEX_HEIGHT; ++y) {
      for (int x = 0; x < TEX_WIDTH; ++x) {
        int index = (y * TEX_WIDTH + x) * 3;
        
        // Normalized coordinates [-1, 1]
        float nx = (float(x) / float(TEX_WIDTH - 1)) * 2.0f - 1.0f;
        float ny = (float(y) / float(TEX_HEIGHT - 1)) * 2.0f - 1.0f;
        
        // Apply rotation to coordinates
        float rx = nx * cos(rotation) - ny * sin(rotation);
        float ry = nx * sin(rotation) + ny * cos(rotation);
        
        // Generate animated patterns
        float wave1 = sin(rx * 8.0f + t * 2.0f) * 0.5f + 0.5f;
        float wave2 = cos(ry * 6.0f + t * 1.5f) * 0.5f + 0.5f;
        float radial = sqrt(rx * rx + ry * ry);
        float ripple = sin(radial * 10.0f - t * 4.0f) * 0.5f + 0.5f;
        
        // Create spiral pattern
        float spiral = atan2(ry, rx) + radial * 3.0f - t * 2.0f;
        float spiralPattern = sin(spiral) * 0.5f + 0.5f;
        
        // Combine patterns
        float r = wave1 * ripple * spiralPattern;
        float g = wave2 * (1.0f - radial * 0.3f);
        float b = (wave1 + wave2) * 0.5f * ripple;
        
        // Add some noise based on frame counter for variation
        float noise = sin(float(state().frameCounter) * 0.1f + rx * ry * 100.0f) * 0.1f;
        r += noise; g += noise; b += noise;
        
        // Clamp and convert to byte values
        r = fmax(0.0f, fmin(1.0f, r));
        g = fmax(0.0f, fmin(1.0f, g));
        b = fmax(0.0f, fmin(1.0f, b));
        
        state().textureData[index + 0] = (unsigned char)(r * 255.0f);
        state().textureData[index + 1] = (unsigned char)(g * 255.0f);
        state().textureData[index + 2] = (unsigned char)(b * 255.0f);
      }
    }
    
    state().textureNeedsUpdate = true;
    
    // Debug output
    if (isPrimary()) {
      std::cout << "Primary: Generated texture at frame " << state().frameCounter 
                << " time=" << state().time << " angle=" << state().angle << std::endl;
    }
  }

  void onAnimate(double dt_sec) override {
    if (isPrimary()) {
      // Update animation time and angle
      state().time += dt_sec;
      state().angle += dt_sec * 90.0f;
      if (state().angle >= 360.0f) state().angle -= 360.0f;
      
      // Generate new texture data periodically
      state().frameCounter++;
      if (state().frameCounter % 30 == 0) { // Update every 30 frames (~0.5 seconds at 60 FPS)
        generateTextureData();
      }
    }
    
    // Always update distributed texture on all nodes when data changes
    // Check if texture data has changed by comparing frame counter
    static int lastFrameCounter = -1;
    if (state().frameCounter != lastFrameCounter) {
      distributedTexture.submit(state().textureData, GL_RGB, GL_UNSIGNED_BYTE);
      lastFrameCounter = state().frameCounter;
      
      // Debug output for secondary nodes
      if (!isPrimary()) {
        std::cout << "Secondary: Updated texture at frame " << state().frameCounter 
                  << " time=" << state().time << " angle=" << state().angle << std::endl;
      }
    }
  }

  void onDraw(Graphics& g) override {
    g.clear(0);

    if (isPrimary()) {
      // Primary: Generate feedback effect with manipulated texture
      
      // 1. Match texture dimensions to window
      texBlur.resize(fbWidth(), fbHeight());

      // 2. Draw feedback texture with manipulation
      g.tint(0.98);
      g.quadViewport(texBlur, -1.005, -1.005, 2.01, 2.01);  // Outward expansion
      g.tint(1);  // reset tint

      // 3. Draw the animated shape
      g.camera(Viewpoint::UNIT_ORTHO);
      g.pushMatrix();
      g.rotate(state().angle * M_PI / 180.0f, 0, 0, 1);
      g.meshColor();
      g.draw(shape);
      g.popMatrix();

      // 4. Draw the distributed texture as overlay (semi-transparent)
      g.tint(0.5);
      g.quadViewport(distributedTexture, -0.3, -0.3, 0.6, 0.6);
      g.tint(1);

      // 5. Copy current frame buffer to feedback texture
      texBlur.copyFrameBuffer();
      
    } else {
      // Secondary: Display only the distributed texture
      g.camera(Viewpoint::UNIT_ORTHO);
      
      // Display the networked texture full screen
      g.quadViewport(distributedTexture, -1, -1, 2, 2);
      
      // Optionally overlay some info or additional graphics
      g.pushMatrix();
      g.rotate(state().angle * M_PI / 180.0f, 0, 0, 1);
      g.scale(0.3);
      g.meshColor();
      g.draw(shape);
      g.popMatrix();
    }
  }
  
  bool onKeyDown(Keyboard const& k) override {
    if (isPrimary()) {
      switch (k.key()) {
        case '1':
          // Faster animation
          break;
        case '2': 
          // Reset animation
          state().time = 0.0f;
          state().angle = 0.0f;
          state().frameCounter = 0;
          break;
        case ' ':
          // Force texture regeneration
          generateTextureData();
          break;
      }
    }
    return true;
  }
};

int main() {
  MyApp app;
  
  // Set window properties
  app.dimensions(800, 600);
  app.title("Networked Texture Demo");
  
  std::cout << "Controls (Primary only):" << std::endl;
  std::cout << "  2 - Reset animation" << std::endl;
  std::cout << "  Space - Force texture update" << std::endl;
  
  app.start();
  
  return 0;
}