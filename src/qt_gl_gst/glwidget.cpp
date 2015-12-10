#include <QMainWindow>
#include "glwidget.h"
#include "shaderlists.h"
#include "applogger.h"

#ifdef GLU_NEEDED
#include "GL/glu.h"
#endif


GLWidget::GLWidget(int argc, char *argv[], QWidget *parent) :
    QGLWidget(QGLFormat(QGL::DoubleBuffer | QGL::DepthBuffer | QGL::Rgba), parent),
    m_closing(false),
    m_brickProg(this)
{
    LOG(LOG_GL, Logger::Debug1, "GLWidget constructor entered");

    m_xRot = 0;
    m_yRot = 0;
    m_zRot = 0;
    m_scaleValue = 1.0;
    m_lastPos = QPoint(0, 0);

    m_rotateOn = 1;
    m_xLastIncr = 0;
    m_yLastIncr = 0;
    m_xInertia = -0.5;
    m_yInertia = 0;

    m_clearColorIndex = 0;

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(animate()));
    timer->start(20);

    grabKeyboard();

    // Video pipeline
    for(int vidIx = 1; vidIx < argc; vidIx++)
    {
        m_videoLoc.push_back(QString(argv[vidIx]));
    }

    m_frames = 0;
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoBufferSwap(false);
    setAutoFillBackground(false);

    m_dataFilesDir = QString(qgetenv(DATA_DIR_ENV_VAR_NAME));
    if(m_dataFilesDir.size() == 0)
    {
        m_dataFilesDir = QString("./");
    }
    else
    {
        m_dataFilesDir += "/";
    }
    LOG(LOG_GL, Logger::Debug1, "m_dataFilesDir = %s", m_dataFilesDir.toUtf8().constData());
}

GLWidget::~GLWidget()
{
}

void GLWidget::initVideo()
{
    // Instantiate video pipeline for each filename specified
    for(int vidIx = 0; vidIx < this->m_videoLoc.size(); vidIx++)
    {
        this->m_vidPipelines.push_back(this->createPipeline(vidIx));

        if(this->m_vidPipelines[vidIx] == NULL)
        {
            LOG(LOG_GL, Logger::Error, "Error creating pipeline for vid %d", vidIx);
            return;
        }

        QObject::connect(this->m_vidPipelines[vidIx], SIGNAL(finished(int)),
                         this, SLOT(pipelineFinished(int)));
        QObject::connect(this, SIGNAL(closeRequested()),
                         this->m_vidPipelines[vidIx], SLOT(Stop()), Qt::QueuedConnection);

        this->m_vidPipelines[vidIx]->Configure();
    }
}

void GLWidget::initializeGL()
{
    QString verStr((const char*)glGetString(GL_VERSION));
    LOG(LOG_GL, Logger::Info, "GL_VERSION: %s", verStr.toUtf8().constData());

    LOG(LOG_GL, Logger::Debug1, "Window is%s double buffered", ((this->format().doubleBuffer()) ? "": " not"));

    qglClearColor(QColor(Qt::black));

    setupShader(&m_I420NoEffect, VidI420NoEffectShaderList, NUM_SHADERS_VIDI420_NOEFFECT);

    glTexParameteri(GL_RECT_VID_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_RECT_VID_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_RECT_VID_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_RECT_VID_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Set uniforms for vid shaders along with other stream details when first
    // frame comes through

    if(m_vidPipelines.size() != m_videoLoc.size())
    {
        LOG(LOG_GL, Logger::Error, "initVideo must be called before intialiseGL");
        return;
    }

    // Create entry in tex info vector for all pipelines
    for(int vidIx = 0; vidIx < this->m_vidPipelines.size(); vidIx++)
    {
        VidTextureInfo newInfo;
        glGenTextures(1, &newInfo.texId);
        newInfo.texInfoValid = false;
        newInfo.buffer = NULL;
        newInfo.effect = VidShaderNoEffect;
        newInfo.frameCount = 0;

        this->m_vidTextures.push_back(newInfo);
    }

    for(int vidIx = 0; vidIx < this->m_vidPipelines.size(); vidIx++)
    {
        this->m_vidPipelines[vidIx]->Start();
    }
}

Pipeline* GLWidget::createPipeline(int vidIx)
{
    return new GStreamerPipeline(vidIx, this->m_videoLoc[vidIx], SLOT(newFrame(int)), this);
}

void GLWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    makeCurrent();

    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    this->m_modelViewMatrix = QMatrix4x4();
    this->m_modelViewMatrix.lookAt(QVector3D(0.0, 0.0, -5.0), QVector3D(0.0, 0.0, 0.0), QVector3D(0.0, 1.0, 0.0));
    this->m_modelViewMatrix.rotate(-m_zRot / 16.0, 0.0, 0.0, 1.0);
    this->m_modelViewMatrix.rotate(-m_xRot / 16.0, 1.0, 0.0, 0.0);
    this->m_modelViewMatrix.rotate(m_yRot / 16.0, 0.0, 1.0, 0.0);
    this->m_modelViewMatrix.scale(m_scaleValue);

    // Draw videos around the object
    for(int vidIx = 0; vidIx < this->m_vidTextures.size(); vidIx++)
    {
        if(this->m_vidTextures[vidIx].texInfoValid)
        {
            // Render a quad with the video on it:
            glActiveTexture(GL_RECT_VID_TEXTURE0);
            glBindTexture(GL_RECT_VID_TEXTURE_2D, this->m_vidTextures[vidIx].texId);
            printOpenGLError(__FILE__, __LINE__);

            this->m_vidTextures[vidIx].shader->bind();
            setVidShaderVars(vidIx, false);
            printOpenGLError(__FILE__, __LINE__);

            QGLShaderProgram *vidShader = this->m_vidTextures[vidIx].shader;

            QMatrix4x4 vidQuadMatrix = this->m_modelViewMatrix;

            vidQuadMatrix.rotate((360/this->m_vidTextures.size())*vidIx, 0.0, 1.0, 0.0);
            vidQuadMatrix.translate(0.0, 0.0, 2.0);

            vidShader->setUniformValue("u_mvp_matrix", m_projectionMatrix * vidQuadMatrix);
            vidShader->setUniformValue("u_mv_matrix", vidQuadMatrix);

            // Need to set these arrays up here as shader instances are shared between
            // all the videos:
            vidShader->enableAttributeArray("a_texCoord");
            vidShader->setAttributeArray("a_texCoord", this->m_vidTextures[vidIx].triStripTexCoords);

            vidShader->enableAttributeArray("a_vertex");
            vidShader->setAttributeArray("a_vertex", this->m_vidTextures[vidIx].triStripVertices);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            vidShader->disableAttributeArray("a_vertex");
            vidShader->disableAttributeArray("a_texCoord");
        }
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.endNativePainting();
    QString framesPerSecond;
    framesPerSecond.setNum(m_frames /(m_frameTime.elapsed() / 1000.0), 'f', 2);
    painter.setPen(Qt::white);
    painter.drawText(20, 40, framesPerSecond + " fps");
    painter.end();
    swapBuffers();

    if (!(m_frames % 100))
    {
        m_frameTime.start();
        m_frames = 0;
    }
    ++m_frames;
}

void GLWidget::resizeGL(int wid, int ht)
{
    float vp = 0.8f;
    float aspect = (float) wid / (float) ht;

    glViewport(0, 0, wid, ht);

    this->m_projectionMatrix = QMatrix4x4();
    this->m_projectionMatrix.frustum(-vp, vp, -vp / aspect, vp / aspect, 1.0, 50.0);
}

void GLWidget::newFrame(int vidIx)
{
    if(this->m_vidPipelines[vidIx])
    {
        LOG(LOG_VIDPIPELINE, Logger::Debug2, "vid %d frame %d",
            vidIx, this->m_vidTextures[vidIx].frameCount++);

        Pipeline *pipeline = this->m_vidPipelines[vidIx];

        /* If we have a vid frame currently, return it back to the video
           system */
        if(this->m_vidTextures[vidIx].buffer)
        {
            pipeline->m_outgoingBufQueue.put(this->m_vidTextures[vidIx].buffer);
            LOG(LOG_VIDPIPELINE, Logger::Debug2, "vid %d pushed buffer %p to outgoing queue",
                vidIx, this->m_vidTextures[vidIx].buffer);
        }

        void *newBuf = NULL;
        if(pipeline->m_incomingBufQueue.get(&newBuf) == true)
        {
            this->m_vidTextures[vidIx].buffer = newBuf;
        }
        else
        {
            this->m_vidTextures[vidIx].buffer = NULL;
            return;
        }

        LOG(LOG_VIDPIPELINE, Logger::Debug2, "vid %d popped buffer %p from incoming queue",
            vidIx, this->m_vidTextures[vidIx].buffer);

        this->makeCurrent();

        // Load the gst buf into a texture
        if(this->m_vidTextures[vidIx].texInfoValid == false)
        {
            LOG(LOG_VIDPIPELINE, Logger::Debug2, "Setting up texture info for vid %d", vidIx);

            // Try and keep this fairly portable to other media frameworks by
            // leaving info extraction within pipeline class
            this->m_vidTextures[vidIx].width = pipeline->getWidth();
            this->m_vidTextures[vidIx].height = pipeline->getHeight();
            this->m_vidTextures[vidIx].colourFormat = pipeline->getColourFormat();

            this->m_vidTextures[vidIx].shader = &m_I420NoEffect;

            this->m_vidTextures[vidIx].shader->bind();
            printOpenGLError(__FILE__, __LINE__);
            // Setting shader variables here will have no effect as they are set on every render,
            // but do it to check for errors, so we don't need to check on every render
            // and program output doesn't go mad
            setVidShaderVars(vidIx, true);

            GLfloat vidWidth = this->m_vidTextures[vidIx].width;
            GLfloat vidHeight = this->m_vidTextures[vidIx].height;

            this->m_vidTextures[vidIx].triStripTexCoords[0]      = QVector2D(vidWidth, 0.0f);
            this->m_vidTextures[vidIx].triStripVertices[0]       = QVector2D(VIDTEXTURE_RIGHT_X, VIDTEXTURE_TOP_Y);

            this->m_vidTextures[vidIx].triStripTexCoords[1]      = QVector2D(0.0f, 0.0f);
            this->m_vidTextures[vidIx].triStripVertices[1]       = QVector2D(VIDTEXTURE_LEFT_X, VIDTEXTURE_TOP_Y);

            this->m_vidTextures[vidIx].triStripTexCoords[2]      = QVector2D(vidWidth, vidHeight);
            this->m_vidTextures[vidIx].triStripVertices[2]       = QVector2D(VIDTEXTURE_RIGHT_X, VIDTEXTURE_BOT_Y);

            this->m_vidTextures[vidIx].triStripTexCoords[3]      = QVector2D(0.0f, vidHeight);
            this->m_vidTextures[vidIx].triStripVertices[3]       = QVector2D(VIDTEXTURE_LEFT_X, VIDTEXTURE_BOT_Y);
        }

        this->m_vidTextures[vidIx].texInfoValid = loadNewTexture(vidIx);

        printOpenGLError(__FILE__, __LINE__);

        this->update();
    }
}

bool GLWidget::loadNewTexture(int vidIx)
{
    bool texLoaded = false;

    glBindTexture (GL_RECT_VID_TEXTURE_2D, this->m_vidTextures[vidIx].texId);
    unsigned char *buffer = this->m_vidPipelines[vidIx]->mapBufToVidDataStart(this->m_vidTextures[vidIx].buffer);

    switch(this->m_vidTextures[vidIx].colourFormat)
    {
        case ColFmt_I420:
            glTexImage2D  (GL_RECT_VID_TEXTURE_2D, 0, GL_LUMINANCE,
                           this->m_vidTextures[vidIx].width,
                           this->m_vidTextures[vidIx].height*1.5f,
                           0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                           buffer);
            texLoaded = true;
            break;
        case ColFmt_UYVY:
            glTexImage2D  (GL_RECT_VID_TEXTURE_2D, 0, GL_LUMINANCE,
                           this->m_vidTextures[vidIx].width*2,
                           this->m_vidTextures[vidIx].height,
                           0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                           buffer);
            texLoaded = true;
            break;
        default:
            LOG(LOG_GL, Logger::Error, "Decide how to load texture for colour format %d",
                this->m_vidTextures[vidIx].colourFormat);
            break;
    };

    this->m_vidPipelines[vidIx]->unmapBufToVidDataStart(this->m_vidTextures[vidIx].buffer);

    return texLoaded;
}

void GLWidget::pipelineFinished(int vidIx)
{
    this->m_vidTextures[vidIx].frameCount = 0;

    if(this->m_closing)
    {
        delete(this->m_vidPipelines[vidIx]);
        this->m_vidPipelines.replace(vidIx, NULL);
        this->m_vidTextures[vidIx].texInfoValid = false;

        // Check if any gst threads left, if not close
        bool allFinished = true;
        for(int i = 0; i < this->m_vidPipelines.size(); i++)
        {
            if(this->m_vidPipelines[i] != NULL)
            {
                // Catch any threads which were already finished at quitting time
                if(this->m_vidPipelines[i]->isFinished())
                {
                    delete(this->m_vidPipelines[vidIx]);
                    this->m_vidPipelines.replace(vidIx, NULL);
                    this->m_vidTextures[vidIx].texInfoValid = false;
                }
                else
                {
                    allFinished = false;
                    break;
                }
            }
        }
        if(allFinished)
        {
            close();
        }
    }
    else
    {
        delete(this->m_vidPipelines[vidIx]);
        //        this->m_vidTextures[vidIx].texInfoValid = false;

        this->m_vidPipelines[vidIx] = createPipeline(vidIx);

        if(this->m_vidPipelines[vidIx] == NULL)
        {
            LOG(LOG_GL, Logger::Error, "Error creating pipeline for vid %d", vidIx);
            return;
        }

        QObject::connect(this->m_vidPipelines[vidIx], SIGNAL(finished(int)),
                         this, SLOT(pipelineFinished(int)));
        QObject::connect(this, SIGNAL(closeRequested()),
                         this->m_vidPipelines[vidIx], SLOT(Stop()), Qt::QueuedConnection);

        this->m_vidPipelines[vidIx]->Configure();
        this->m_vidPipelines[vidIx]->Start();
    }
}

// Layout size
QSize GLWidget::minimumSizeHint() const
{
    return QSize(50, 50);
}

QSize GLWidget::sizeHint() const
{
    return QSize(400, 400);
}

// Animation
static int qNormalizeAngle(int angle)
{
    while (angle < 0)
        angle += 360 * 16;
    while (angle > 360 * 16)
        angle -= 360 * 16;

    return angle;
}

void GLWidget::animate()
{
    /* Increment wrt inertia */
    if (m_rotateOn)
    {
        m_xRot = qNormalizeAngle(m_xRot + (8 * m_yInertia));
        m_yRot = qNormalizeAngle(m_yRot + (8 * m_xInertia));
    }

    update();
}

void GLWidget::resetPosSlot()
{
    m_xRot = 0;
    m_yRot = 35;
    m_zRot = 0;
    m_xLastIncr = 0;
    m_yLastIncr = 0;
    m_xInertia = -0.5;
    m_yInertia = 0;
    m_scaleValue    = 1.0;
}

void GLWidget::exitSlot()
{
    close();
}

void GLWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastPos = event->pos();

    if (event->button() == Qt::LeftButton)
    {
        m_xInertia = 0;
        m_yInertia = 0;

        m_xLastIncr = 0;
        m_yLastIncr = 0;
    }
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        // Left button released
        m_lastPos.setX(-1);
        m_lastPos.setY(-1);

        if (m_xLastIncr > INERTIA_THRESHOLD)
            m_xInertia = (m_xLastIncr - INERTIA_THRESHOLD)*INERTIA_FACTOR;

        if (-m_xLastIncr > INERTIA_THRESHOLD)
            m_xInertia = (m_xLastIncr + INERTIA_THRESHOLD)*INERTIA_FACTOR;

        if (m_yLastIncr > INERTIA_THRESHOLD)
            m_yInertia = (m_yLastIncr - INERTIA_THRESHOLD)*INERTIA_FACTOR;

        if (-m_yLastIncr > INERTIA_THRESHOLD)
            m_yInertia = (m_yLastIncr + INERTIA_THRESHOLD)*INERTIA_FACTOR;

    }
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if((m_lastPos.x() != -1) && (m_lastPos.y() != -1))
    {
        m_xLastIncr = event->x() - m_lastPos.x();
        m_yLastIncr = event->y() - m_lastPos.y();

        if ((event->modifiers() & Qt::ControlModifier)
                || (event->buttons() & Qt::RightButton))
        {
            if (m_lastPos.x() != -1)
            {
                m_zRot = qNormalizeAngle(m_zRot + (8 * m_xLastIncr));
                m_scaleValue += (m_yLastIncr)*SCALE_FACTOR;
                update();
            }
        }
        else
        {
            if (m_lastPos.x() != -1)
            {
                m_xRot = qNormalizeAngle(m_xRot + (8 * m_yLastIncr));
                m_yRot = qNormalizeAngle(m_yRot + (8 * m_xLastIncr));
                update();
            }
        }
    }

    m_lastPos = event->pos();
}

void GLWidget::keyPressEvent(QKeyEvent *e)
{
    switch(e->key())
    {
        case Qt::Key_Question:
        case Qt::Key_H:
            std::cout <<  "\nKeyboard commands:\n\n"
                          "? - Help\n"
                          "q, <esc> - Quit\n"
                          "b - Toggle among background clear colors\n"
                          "m - Load a different model to render\n"
                          "s - "
                          "a - "
                          "v - "
                          "o - "
                          "p - "
                          "<home>     - reset zoom and rotation\n"
                          "<space> or <click>        - stop rotation\n"
                          "<+>, <-> or <ctrl + drag> - zoom model\n"
                          "<arrow keys> or <drag>    - rotate model\n"
                          "\n";
            break;
        case Qt::Key_Escape:
        case Qt::Key_Q:
            exitSlot();
            break;

        case Qt::Key_Plus:
            m_scaleValue += SCALE_INCREMENT;
            break;
        case Qt::Key_Minus:
            m_scaleValue -= SCALE_INCREMENT;
            break;
        case Qt::Key_Home:
            resetPosSlot();
            break;
        case Qt::Key_Left:
            m_yRot -= 8;
            break;
        case Qt::Key_Right:
            m_yRot += 8;
            break;
        case Qt::Key_Up:
            m_xRot -= 8;
            break;
        case Qt::Key_Down:
            m_xRot += 8;
            break;

        default:
            QGLWidget::keyPressEvent(e);
            break;
    }
}

void GLWidget::closeEvent(QCloseEvent* event)
{
    if(this->m_closing == false)
    {
        this->m_closing = true;
        emit closeRequested();

        // Just in case, check now if any gst threads still exist, if not, close application now
        bool allFinished = true;
        for(int i = 0; i < this->m_vidPipelines.size(); i++)
        {
            if(this->m_vidPipelines[i] != NULL)
            {
                allFinished = false;
                break;
            }
        }
        if(allFinished)
        {
            close();
        }
        event->ignore();
    }
    else
    {
        // This is where we're all finished and really are m_closing now.
        // At the mo, tell parent to close too.
        QWidget* _parent = dynamic_cast<QWidget*>(parent());
        if(_parent)
            _parent->close();
    }
}

// Shader WILL be all set up for the specified video texture when this is called,
// or else!
void GLWidget::setVidShaderVars(int vidIx, bool printErrors)
{
    // TODO: move common vars out of switch

    switch(this->m_vidTextures[vidIx].effect)
    {
        case VidShaderNoEffect:
        case VidShaderNoEffectNormalisedTexCoords:
            // Temp:
            printOpenGLError(__FILE__, __LINE__);

            this->m_vidTextures[vidIx].shader->setUniformValue("u_vidTexture", 0); // texture unit index
            // Temp:
            printOpenGLError(__FILE__, __LINE__);
            this->m_vidTextures[vidIx].shader->setUniformValue("u_yHeight", (GLfloat)this->m_vidTextures[vidIx].height);
            // Temp:
            printOpenGLError(__FILE__, __LINE__);
            this->m_vidTextures[vidIx].shader->setUniformValue("u_yWidth", (GLfloat)this->m_vidTextures[vidIx].width);

            if(printErrors) printOpenGLError(__FILE__, __LINE__);
            break;

        default:
            LOG(LOG_GLSHADERS, Logger::Warning, "Invalid effect set on vidIx %d", vidIx);
            break;
    }
}

int GLWidget::loadShaderFile(QString fileName, QString &shaderSource)
{
    fileName = m_dataFilesDir + fileName;

    shaderSource.clear();
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        LOG(LOG_GLSHADERS, Logger::Error, "File '%s' does not exist!", qPrintable(fileName));
        return -1;
    }

    QTextStream in(&file);
    while (!in.atEnd())
    {
        shaderSource += in.readLine();
        shaderSource += "\n";
    }

    return 0;
}

int GLWidget::setupShader(QGLShaderProgram *prog, GLShaderModule shaderList[], int listLen)
{
    bool ret;

    LOG(LOG_GLSHADERS, Logger::Debug1, "-- Setting up a new full shader: --");

    QString shaderSource;
    QString shaderSourceFileNames;
    QString fullShaderSourceFileNames;
    for(int listIx = 0; listIx < listLen; listIx++)
    {
        if(shaderList[listIx].type == QGLShader::Vertex)
        {
            QString nextShaderSource;

            LOG(LOG_GLSHADERS, Logger::Debug1, "concatenating %s", shaderList[listIx].sourceFileName);
            shaderSourceFileNames += shaderList[listIx].sourceFileName;
            shaderSourceFileNames += ", ";
            fullShaderSourceFileNames += shaderList[listIx].sourceFileName;
            fullShaderSourceFileNames += ", ";

            ret = loadShaderFile(shaderList[listIx].sourceFileName, nextShaderSource);
            if(ret != 0)
            {
                return ret;
            }

            shaderSource += nextShaderSource;
        }
    }

    if(!shaderSource.isEmpty())
    {
        LOG(LOG_GLSHADERS, Logger::Debug1, "compiling vertex shader");

        ret = prog->addShaderFromSourceCode(QGLShader::Vertex, shaderSource);

        if(ret == false)
        {
            LOG(LOG_GLSHADERS, Logger::Error, "Compile log for vertex shader sources %s:\n%s\n",
                shaderSourceFileNames.toUtf8().constData(),
                prog->log().toUtf8().constData());
            return -1;
        }
    }

    shaderSource.clear();
    shaderSourceFileNames.clear();

    for(int listIx = 0; listIx < listLen; listIx++)
    {
        if(shaderList[listIx].type == QGLShader::Fragment)
        {
            QString nextShaderSource;

            LOG(LOG_GLSHADERS, Logger::Debug1, "concatenating %s", shaderList[listIx].sourceFileName);
            shaderSourceFileNames += shaderList[listIx].sourceFileName;
            shaderSourceFileNames += ", ";
            fullShaderSourceFileNames += shaderList[listIx].sourceFileName;
            fullShaderSourceFileNames += ", ";

            ret = loadShaderFile(shaderList[listIx].sourceFileName, nextShaderSource);
            if(ret != 0)
            {
                return ret;
            }

            shaderSource += nextShaderSource;
        }
    }

    if(!shaderSource.isEmpty())
    {
        LOG(LOG_GLSHADERS, Logger::Debug1, "compiling fragment shader");

        ret = prog->addShaderFromSourceCode(QGLShader::Fragment, shaderSource);

        if(ret == false)
        {
            LOG(LOG_GLSHADERS, Logger::Error, "Compile log for fragment shader sources %s:\n%s\n",
                shaderSourceFileNames.toUtf8().constData(),
                prog->log().toUtf8().constData());
            return -1;
        }
    }

    ret = prog->link();
    if(ret == false)
    {
        LOG(LOG_GLSHADERS, Logger::Error, "Link log for shader sources %s:\n%s\n",
            fullShaderSourceFileNames.toUtf8().constData(),
            prog->log().toUtf8().constData());
        return -1;
    }

    ret = prog->bind();
    if(ret == false)
    {
        LOG(LOG_GLSHADERS, Logger::Error, "Error binding shader from sources %s",
            fullShaderSourceFileNames.toUtf8().constData());
        return -1;
    }

    printOpenGLError(__FILE__, __LINE__);

    return 0;
}


int GLWidget::printOpenGLError(const char *file, int line)
{
    //
    // Returns 1 if an OpenGL error occurred, 0 otherwise.
    //
    GLenum glErr;
    int    retCode = 0;

    glErr = glGetError();
    while (glErr != GL_NO_ERROR)
    {
        LOG(LOG_GL, Logger::Error, "glError in file %s:%d : %s", file, line, (const char *)gluErrorString(glErr));
        retCode = 1;
        glErr = glGetError();
    }
    return retCode;
}
