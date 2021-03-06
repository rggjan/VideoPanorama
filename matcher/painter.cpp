#include "painter.hpp"

#include <iostream>

// Shader sources
const GLchar* vertexSource = R"(
    #version 300 es
    in vec2 position;
    in vec2 texcoord;
    out vec2 Texcoord;
    void main() {
      Texcoord = texcoord;
      gl_Position = vec4(position, 0.0, 1.0);
    }
)";
const GLchar* fragmentSource = R"(
    #version 300 es
    precision mediump float;
    in vec2 Texcoord;
    out vec4 outColor;
    uniform sampler2D image;
    uniform mat3 H;

    void main() {
       vec3 newPos = H*vec3(Texcoord, 1.0);
       vec2 newPosNormalized = newPos.xy/newPos.z;
       if (newPosNormalized.x > 1.0 || newPosNormalized.y > 1.0 || newPosNormalized.x < 0.0 || newPosNormalized.y < 0.0)
       {
         discard;
         return;
       }
       outColor = vec4(texture(image, newPosNormalized).bgr, 1.0);
    }
)";


Painter::Painter()
{
  m_H1 = cv::Mat::eye(3, 3, CV_32FC1);
  m_H2 = cv::Mat::eye(3, 3, CV_32FC1);
}

Painter::~Painter()
{

}

void Painter::updateImage1(cv::Mat image)
{
  m_image1 = image;
}

void Painter::updateImage2(cv::Mat image)
{
  m_image2 = image;
}

void Painter::updateHomography1(cv::Mat H)
{
  m_H1 = H.clone();
}

void Painter::updateHomography2(cv::Mat H)
{
  m_H2 = H.clone();
}

void _check_gl_error(const char* file, int line) {
        GLenum err (glGetError());

        while(err!=GL_NO_ERROR) {
                std::string error;

                switch(err) {
                        case GL_INVALID_OPERATION:      error="INVALID_OPERATION";      break;
                        case GL_INVALID_ENUM:           error="INVALID_ENUM";           break;
                        case GL_INVALID_VALUE:          error="INVALID_VALUE";          break;
                        case GL_OUT_OF_MEMORY:          error="OUT_OF_MEMORY";          break;
                        case GL_INVALID_FRAMEBUFFER_OPERATION:  error="INVALID_FRAMEBUFFER_OPERATION";  break;
                }

                std::cerr << "GL_" << error.c_str() <<" - "<<file<<":"<<line<<std::endl;
                err=glGetError();
        }
}

#define GL_CHECK() _check_gl_error(__FILE__,__LINE__)

void Painter::setupOpenGL(int w, int h)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Create Vertex Array Object
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  // Create a Vertex Buffer Object and copy the vertex data to it
  GLuint vbo;
  glGenBuffers(1, &vbo);

  GLfloat vertices[] = {
    // Position   Texcoords
    -1.0f,  1.0f, 0.0f, 0.0f, // Top-left
     1.0f,  1.0f, 1.0f, 0.0f, // Top-right
     1.0f, -1.0f, 1.0f, 1.0f, // Bottom-right
    -1.0f, -1.0f, 0.0f, 1.0f  // Bottom-left
  };

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  // Create an element array
  GLuint ebo;
  glGenBuffers(1, &ebo);

  GLuint elements[] = {
    0, 1, 2,
    2, 3, 0
  };

  GL_CHECK();

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(elements), elements, GL_STATIC_DRAW);

  // Create and compile the vertex shader
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexSource, NULL);
  glCompileShader(vertexShader);

  GLint status;
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);

  if (status == GL_FALSE)
  {
    GLint infoLogLength;
    glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &infoLogLength);

    GLchar* strInfoLog = new GLchar[infoLogLength + 1];
    glGetShaderInfoLog(vertexShader, infoLogLength, NULL, strInfoLog);

    printf("Compilation error in shader: %s\n", strInfoLog);
    delete[] strInfoLog;
  }

  GL_CHECK();

  // Create and compile the fragment shader
  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);

  if (status == GL_FALSE)
  {
    GLint infoLogLength;
    glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &infoLogLength);

    GLchar* strInfoLog = new GLchar[infoLogLength + 1];
    glGetShaderInfoLog(fragmentShader, infoLogLength, NULL, strInfoLog);

    printf("Compilation error in shader: %s\n", strInfoLog);
    delete[] strInfoLog;
  }

  GL_CHECK();

  // Link the vertex and fragment shader into a shader program
  m_shaderProgram = glCreateProgram();
  glAttachShader(m_shaderProgram, vertexShader);
  glAttachShader(m_shaderProgram, fragmentShader);
  glLinkProgram(m_shaderProgram);

    GL_CHECK();

  GLint isLinked = 0;
  glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &isLinked);
  if(isLinked == GL_FALSE)
  {
    GLint maxLength = 0;
    glGetProgramiv(m_shaderProgram, GL_INFO_LOG_LENGTH, &maxLength);

    //The maxLength includes the NULL character
    std::vector<GLchar> infoLog(maxLength);
    glGetProgramInfoLog(m_shaderProgram, maxLength, &maxLength, &infoLog[0]);

    printf("Error: %s\n", &infoLog[0]);

    //The program is useless now. So delete it.
    glDeleteProgram(m_shaderProgram);

    //Provide the infolog in whatever manor you deem best.
    //Exit with failure.
    exit(3);
  }


  glUseProgram(m_shaderProgram);

  GL_CHECK();

  // Specify the layout of the vertex data
  GLint posAttrib = glGetAttribLocation(m_shaderProgram, "position");
  glEnableVertexAttribArray(posAttrib);
  glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);

  GL_CHECK();

  GLint texAttrib = glGetAttribLocation(m_shaderProgram, "texcoord");
  glEnableVertexAttribArray(texAttrib);
  glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

  GL_CHECK();

  // Load textures
  glGenTextures(2, m_textures);

  glUniform1i(glGetUniformLocation(m_shaderProgram, "image"), 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_textures[0]);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  GL_CHECK();

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_textures[1]);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  GL_CHECK();
}

void Painter::draw()
{
  std::lock_guard<std::mutex> lock(m_mutex);


  // Clear the screen to black
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glActiveTexture(GL_TEXTURE0);

   
   
   if (!m_image2.empty())
   {
      glBindTexture(GL_TEXTURE_2D, m_textures[1]);
      
      GL_CHECK();
      if (m_texture2Created)
      {
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_image2.size().width, m_image2.size().height, m_image2.channels() == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, m_image2.data);
         GL_CHECK();
      }
      else
      {
         glTexImage2D(GL_TEXTURE_2D, 0, m_image2.channels() == 3 ? GL_RGB : GL_RGBA, m_image2.size().width, m_image2.size().height, 0, m_image2.channels() == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, m_image2.data);
         m_texture2Created = true;
      }
      
      GLint location = glGetUniformLocation(m_shaderProgram, "H");
      if (location == -1)
      {
         std::cout << "Cannot get H" << std::endl;
      }
      else
      {
         glUniformMatrix3fv(location, 1 /*only setting 1 matrix*/, true /*transpose?*/, (float*)convert(m_H2, m_image2.size().width, m_image2.size().height).data);
      }
      
      // Draw a rectangle from the 2 triangles using 6 indices
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
   }
   
   
   if (!m_image1.empty())
   {
      glBindTexture(GL_TEXTURE_2D, m_textures[0]);
      GL_CHECK();
      
      if (m_texture1Created)
      {
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_image1.size().width, m_image1.size().height, m_image1.channels() == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, m_image1.data);
         GL_CHECK();
      }
      else
      {
         glTexImage2D(GL_TEXTURE_2D, 0, m_image1.channels() == 3 ? GL_RGB : GL_RGBA, m_image1.size().width, m_image1.size().height, 0, m_image1.channels() == 3 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, m_image1.data);
         
         GL_CHECK();
         
         m_texture1Created = true;
      }
      
      GLint location = glGetUniformLocation(m_shaderProgram, "H");
      if (location == -1)
      {
      }
      else
      {
         glUniformMatrix3fv(location, 1 /*only setting 1 matrix*/, true /*transpose?*/, (float*)convert(m_H1, m_image1.size().width, m_image1.size().height).data);
      }
      
      // Draw a rectangle from the 2 triangles using 6 indices
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
   }


  GL_CHECK();
}

void Painter::cleanupOpenGL()
{
  std::lock_guard<std::mutex> lock(m_mutex);
}

cv::Mat Painter::convert(cv::Mat input, int width, int height)
{
  float dataTextureScale[9] = {2.0, 0, -0.5,
                  0, 2.0, -0.5,
                  0, 0, 1};
  cv::Mat textureScale(3, 3, CV_32FC1, &dataTextureScale);

  float dataImageToTexture[9] = {1.f/width, 0, 0,
                  0, 1.f/height, 0,
                  0, 0, 1};
  cv::Mat imageToTexture(3, 3, CV_32FC1, &dataImageToTexture);

  cv::Mat inputFloat;
  input.convertTo(inputFloat, CV_32FC1);

  return imageToTexture * inputFloat.inv() * imageToTexture.inv() * textureScale;
}
