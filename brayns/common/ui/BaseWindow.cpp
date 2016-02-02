/* Copyright (c) 2011-2015, EPFL/Blue Brain Project
 *                     Cyrille Favreau <cyrille.favreau@epfl.ch>
 *                     Jafet Villafranca <jafet.villafrancadiaz@epfl.ch>
 *
 * This file is part of BRayns
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "BaseWindow.h"

#include <brayns/Brayns.h>
#include <brayns/common/log.h>
#include <brayns/common/parameters/ParametersManager.h>
#include <brayns/common/scene/Scene.h>
#include <brayns/common/camera/Camera.h>

#include <assert.h>

#ifdef __APPLE__
#  include "GLUT/glut.h"
#  include <unistd.h>
#else
#  include "GL/glut.h"
#endif

namespace brayns
{

const float DEFAULT_EPSILON = 1e-4f;
const float DEFAULT_MOTION_ACCELERATION = 1.5f;
const float DEFAULT_MOUSE_SPEED = 0.005f;

void runGLUT( )
{
    glutMainLoop( );
}

void initGLUT(int32 *ac, const char **av)
{
    glutInit(ac, (char **) av);
}

// ------------------------------------------------------------------
// glut event handlers
// ------------------------------------------------------------------
void glut3dReshape(int32 x, int32 y)
{
    if (BaseWindow::_activeWindow)
        BaseWindow::_activeWindow->reshape(Vector2i(x,y));
}

void glut3dDisplay( void )
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->display( );
}

void glut3dKeyboard(unsigned char key, int32 x, int32 y)
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->keypress(key,Vector2i(x,y));
}
void glut3dSpecial(int32 key, int32 x, int32 y)
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->specialkey(key,Vector2i(x,y));
}

void glut3dIdle( void )
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->idle( );
}
void glut3dMotionFunc(int32 x, int32 y)
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->motion(Vector2i(x,y));
}

void glut3dMouseFunc(int32 whichButton, int32 released, int32 x, int32 y)
{
    if(BaseWindow::_activeWindow)
       BaseWindow::_activeWindow->mouseButton(
                   whichButton, released, Vector2i(x,y));
}

// ------------------------------------------------------------------
// Base window
// ------------------------------------------------------------------
/*! currently active window */
BaseWindow *BaseWindow::_activeWindow = nullptr;

BaseWindow::BaseWindow(
        const int argc, const char **argv,
        const FrameBufferMode frameBufferMode,
        const ManipulatorMode initialManipulator,
        const int allowedManipulators)
  : _lastMousePos(-1,-1), _currMousePos(-1,-1),
    _lastButtonState(0), _currButtonState(0),
    _currModifiers(0), _upVectorFromCmdLine(0,1,0),
    _motionSpeed(DEFAULT_MOUSE_SPEED), _rotateSpeed(DEFAULT_MOUSE_SPEED),
    _frameBufferMode(frameBufferMode),
    _windowID(-1), _windowSize(-1,-1), fullScreen_(false),
    frameCounter_(0)
{
    // Initialize brayns
    _brayns.reset(new Brayns(argc, argv));
    setViewPort( );

    // Initialize manipulators
    if(allowedManipulators & INSPECT_CENTER_MODE)
        _inspectCenterManipulator.reset(new InspectCenterManipulator(*this));

    if(allowedManipulators & MOVE_MODE)
        _flyingModeManipulator.reset(new FlyingModeManipulator(*this));

    switch(initialManipulator)
    {
    case MOVE_MODE:
        _manipulator.reset(_flyingModeManipulator.get( ));
        break;
    case INSPECT_CENTER_MODE:
        _manipulator.reset(_inspectCenterManipulator.get( ));
        break;
    }
    assert(_manipulator);
}


BaseWindow::~BaseWindow( )
{
}

void BaseWindow::mouseButton(
        int32 whichButton,
        bool released,
        const Vector2i& pos)
{
    if(pos != _currMousePos)
        motion(pos);
    _lastButtonState = _currButtonState;

    if(released)
        _currButtonState = _currButtonState & ~(1<<whichButton);
    else
        _currButtonState = _currButtonState |  (1<<whichButton);
    _currModifiers = glutGetModifiers( );

    _manipulator->button( pos );
}

void BaseWindow::motion(const Vector2i& pos)
{
    _currMousePos = pos;
    if(_currButtonState != _lastButtonState)
    {
        // some button got pressed; reset 'old' pos to new pos.
        _lastMousePos = _currMousePos;
        _lastButtonState = _currButtonState;
    }

    _manipulator->motion( );
    _lastMousePos = _currMousePos;
    if(_viewPort.getModified( ))
    {
        _brayns->commit( );
        forceRedraw( );
    }
}

void BaseWindow::idle( )
{
    usleep(1000);
}

void BaseWindow::reshape(const Vector2i& newSize)
{
    _windowSize = newSize;
    _viewPort.setAspect(float(newSize.x( ))/float(newSize.y( )));
    _brayns->reshape(newSize);
    forceRedraw( );
}

void BaseWindow::activate( )
{
    _activeWindow = this;
    glutSetWindow(_windowID);
}

void BaseWindow::forceRedraw( )
{
    glutPostRedisplay( );
}

void BaseWindow::display( )
{
    RenderInput renderInput;
    RenderOutput renderOutput;

    renderInput.position = _viewPort.getPosition( );
    renderInput.target = _viewPort.getTarget( )-_viewPort.getPosition( );
    renderInput.up = _viewPort.getUp( );

    _brayns->render(renderInput, renderOutput);

    GLenum format = GL_RGBA;
    GLenum type   = GL_FLOAT;
    GLvoid* buffer = 0;
    switch(_frameBufferMode)
    {
    case BaseWindow::FRAMEBUFFER_COLOR:
        type = GL_UNSIGNED_BYTE;
        buffer = renderOutput.colorBuffer.data( );
        break;
    case BaseWindow::FRAMEBUFFER_DEPTH:
        format = GL_LUMINANCE;
        buffer = renderOutput.depthBuffer.data( );
        break;
    default:
        glClearColor(0.f,0.f,0.f,1.f);
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    }

    glDrawPixels(_windowSize.x( ), _windowSize.y( ), format, type, buffer);
    glutSwapBuffers( );

#if(BRAYNS_USE_REST || BRAYNS_USE_DEFLECT)
    _viewPort.setPosition(_brayns->getCamera( ).getPosition( ));
#endif
    ++frameCounter_;
}

void BaseWindow::clearPixels( )
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glutSwapBuffers( );
}

void BaseWindow::drawPixels(const uint32* framebuffer)
{
    glDrawPixels(_windowSize.x( ), _windowSize.y( ),
                 GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);
    glutSwapBuffers( );
}

void BaseWindow::drawPixels(const Vector3f* framebuffer)
{
    glDrawPixels(_windowSize.x( ), _windowSize.y( ),
                 GL_RGBA, GL_FLOAT, framebuffer);
    glutSwapBuffers( );
}

Boxf BaseWindow::getWorldBounds( )
{
    return _brayns->getScene( ).getWorldBounds( );
}

void BaseWindow::setViewPort( )
{
    Boxf worldBounds = _brayns->getScene( ).getWorldBounds( );
    Vector3f center = worldBounds.getCenter( );
    Vector3f diag   = worldBounds.getDimension( );
    diag = max(diag,Vector3f(0.3f*diag.length( )));
    Vector3f from = center;
    from.z( ) -= diag.z( );

    Vector3f up  = Vector3f(0.f,1.f,0.f);

    _viewPort.initialize(from, center, up);

    _motionSpeed = diag.length( ) * 0.001f;
    BRAYNS_INFO << "World bounds :" << worldBounds << std::endl;
    BRAYNS_INFO << "Viewport     :" << _viewPort << std::endl;
    BRAYNS_INFO << "Motion speed :" << _motionSpeed << std::endl;
}

void BaseWindow::setTitle(const char *title)
{
    assert(_windowID>=0);
    glutSetWindow( _windowID );
    glutSetWindowTitle( title );
}

void BaseWindow::create(const char *title,
                        const size_t width, const size_t height,
                        bool fullScreen)
{
    glutInitWindowSize(width, height);
    _windowID = glutCreateWindow(title);
    _activeWindow = this;
    glutDisplayFunc(glut3dDisplay);
    glutReshapeFunc(glut3dReshape);
    glutKeyboardFunc(glut3dKeyboard);
    glutSpecialFunc(glut3dSpecial);
    glutMotionFunc(glut3dMotionFunc);
    glutMouseFunc(glut3dMouseFunc);
    glutIdleFunc(glut3dIdle);

    if(fullScreen)
        glutFullScreen( );
}

void BaseWindow::specialkey( int32 key, const Vector2f& )
{
    if(_manipulator)
        _manipulator->specialkey( key );
}

void BaseWindow::keypress( char key, const Vector2f& )
{
    RenderingParameters& renderParams =
        _brayns->getParametersManager( ).getRenderingParameters( );

    switch( key )
    {
    case '+':
        _motionSpeed *= DEFAULT_MOTION_ACCELERATION;
        BRAYNS_INFO << "Motion speed: " << _motionSpeed << std::endl;
        break;
    case '-':
        _motionSpeed /= DEFAULT_MOTION_ACCELERATION;
        BRAYNS_INFO << "Motion speed: " << _motionSpeed << std::endl;
        break;
    case '1':
        renderParams.setBackgroundColor(Vector3f(.5f,.5f,.5f));
        BRAYNS_INFO << "Setting grey background" << std::endl;
        break;
    case '2':
        renderParams.setBackgroundColor(Vector3f(1.f,1.f,1.f));
        BRAYNS_INFO << "Setting white background" << std::endl;
        break;
    case '3':
        renderParams.setBackgroundColor(Vector3f(0.f,0.f,0.f));
        BRAYNS_INFO << "Setting black background" << std::endl;
        break;
    case 'C':
        BRAYNS_INFO << _viewPort << std::endl;
        break;
    case 'D':
        renderParams.setDepthOfField( !renderParams.getDepthOfField( ));
        BRAYNS_INFO << "Detph of field: " <<
            renderParams.getDepthOfField( ) << std::endl;
        break;
    case 'E':
        renderParams.setElectronShading( !renderParams.getElectronShading( ));
        BRAYNS_INFO << "Electron shading: " <<
            (renderParams.getElectronShading( ) ? "On" : "Off") << std::endl;
        break;
    case 'F':
        // 'f'ly mode
        if( _flyingModeManipulator )
        {
            BRAYNS_INFO << "Switching to flying mode" << std::endl;
            _manipulator = std::move(_flyingModeManipulator);
        }
        break;
    case 'G':
        renderParams.setGradientBackground(
            !renderParams.getGradientBackground( ));
        BRAYNS_INFO << "Gradient background: " <<
            (renderParams.getGradientBackground( ) ? "On" : "Off") << std::endl;
        break;
    case 'H':
        renderParams.setSoftShadows( !renderParams.getSoftShadows( ));
        BRAYNS_INFO << "Soft shadows " <<
            (renderParams.getSoftShadows( ) ? "On" : "Off") << std::endl;
        break;
    case 'I':
        // 'i'nspect mode
        if( _inspectCenterManipulator)
        {
            BRAYNS_INFO << "Switching to inspect mode" << std::endl;
            _manipulator = std::move(_inspectCenterManipulator);
        }
        break;
    case 'L':
    {
        fullScreen_ = !fullScreen_;
        if(fullScreen_)
            glutFullScreen( );
        else
            glutPositionWindow(0,10);
        break;
    }
    case 'o':
    {
        float aaStrength = _brayns->getParametersManager( ).
            getRenderingParameters( ).getAmbientOcclusionStrength( );
        aaStrength += 0.1f;
        if( aaStrength>1.f ) aaStrength=1.f;
        renderParams.setAmbientOcclusionStrength( aaStrength );
        BRAYNS_INFO << "Ambient occlusion strength: " <<
            aaStrength << std::endl;
        break;
    }
    case 'O':
    {
        float aaStrength = renderParams.getAmbientOcclusionStrength( );
        aaStrength -= 0.1f;
        if( aaStrength<0.f ) aaStrength=0.f;
        renderParams.setAmbientOcclusionStrength( aaStrength );
        BRAYNS_INFO << "Ambient occlusion strength: "
            << aaStrength << std::endl;
        break;
    }
    case 'P':
        renderParams.setLightShading( !renderParams.getLightShading( ));
        BRAYNS_INFO << "Light shading: " <<
            (renderParams.getLightShading( ) ? "On" : "Off") << std::endl;
        break;
    case 'r':
        renderParams.setFrameNumber(0);
        BRAYNS_INFO << "Frame number: " <<
            renderParams.getFrameNumber( ) << std::endl;
        break;
    case 'R':
        renderParams.setFrameNumber(std::numeric_limits<uint16_t>::max( ));
        BRAYNS_INFO << "Frame number: " <<
            renderParams.getFrameNumber( ) << std::endl;
        break;
    case 'S':
        renderParams.setShadows(
            !renderParams.getShadows( ));
        BRAYNS_INFO << "Shadows: " <<
            (renderParams.getShadows( ) ? "On" : "Off") << std::endl;
        break;
    case 'V':
        renderParams.
            setBackgroundColor( Vector3f( rand( ) % 200 / 100.f - 1.f,
            rand( ) % 200 / 100.f - 1.f, rand( ) % 200 / 100.f - 1.f ));
        break;
    case 'Y':
        renderParams.setLightEmittingMaterials(
            !renderParams.getLightEmittingMaterials( ));
        break;
    case 'Z':
        if( _frameBufferMode==FRAMEBUFFER_DEPTH )
            _frameBufferMode = FRAMEBUFFER_COLOR;
        else
            _frameBufferMode = FRAMEBUFFER_DEPTH;
        break;
    }
    if(_manipulator)
        _manipulator->keypress( key );
}

}