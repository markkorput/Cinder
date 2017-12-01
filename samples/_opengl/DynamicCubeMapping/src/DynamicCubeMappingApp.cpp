#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/ImageIo.h"
#include "cinder/gl/gl.h"
#include "cinder/ImageIo.h"
#include "cinder/gl/Texture.h"

using namespace ci;
using namespace ci::app;
using namespace std;

struct Satellite {
	vec3		mPos;
	Colorf		mColor;
};

class CubeMapper;
using CubeMapperRef=std::shared_ptr<CubeMapper>;

class CubeMapper {
public:

	static CubeMapperRef create(){
		return create(1024,1024);
	}

	static CubeMapperRef create(int width, int height){
		return std::make_shared<CubeMapper>(gl::FboCubeMap::create( width, height ));
	}

	static CubeMapperRef create(int width, int height, const gl::FboCubeMap::Format &format){
		return std::make_shared<CubeMapper>(gl::FboCubeMap::create( width, height, format ));
	}

public:

	CubeMapper(gl::FboCubeMapRef fbo) : mFbo(fbo){
	}

	void update(){
		gl::pushViewport( ivec2( 0, 0 ), mFbo->getSize() );
		// we need to save the current FBO because we'll be calling bindFramebufferFace() below
		gl::context()->pushFramebuffer();
		for( uint8_t dir = 0; dir < 6; ++dir ) {
			gl::setProjectionMatrix( ci::CameraPersp( mFbo->getWidth(), mFbo->getHeight(), 90.0f, 1, 1000 ).getProjectionMatrix() );
			gl::setViewMatrix( mFbo->calcViewMatrix( GL_TEXTURE_CUBE_MAP_POSITIVE_X + dir, vec3( 0 ) ) );
			mFbo->bindFramebufferFace( GL_TEXTURE_CUBE_MAP_POSITIVE_X + dir );

			drawSignal.emit();
		}
		// restore the FBO before we bound the various faces of the CubeMapFbo
		gl::context()->popFramebuffer();
		gl::popViewport();
	}

	gl::FboCubeMapRef getFboCubeMap() {
		return mFbo;
	}

	void writeImageHorizontalCross(const fs::path &path){
		auto tmpFbo = gl::Fbo::create(mFbo->getWidth()*4, mFbo->getHeight()*3);

		{
			gl::ScopedFramebuffer fbScp(tmpFbo);
			gl::pushViewport( ivec2( 0, 0 ), tmpFbo->getSize() );
			// we need to save the current FBO because we'll be calling bindFramebufferFace() below
			gl::context()->pushFramebuffer();


			{
				gl::setMatricesWindow( tmpFbo->getWidth(), tmpFbo->getHeight() );
				gl::ScopedDepth d( false );

				gl::clear(ColorA(0,0,0,0));

				gl::drawHorizontalCross( mFbo->getTextureCubeMap(), Rectf( 0, 0, tmpFbo->getWidth(), tmpFbo->getHeight() ) ); // try this alternative

				// restore the FBO before we bound the various faces of the CubeMapFbo
				gl::context()->popFramebuffer();
				gl::popViewport();
			}
		}

		writeImage( writeFile(path), tmpFbo->getColorTexture()->createSource() );
	}

	void writeImageEquirectangular(const fs::path &path){
		auto tmpFbo = gl::Fbo::create(mFbo->getWidth()*4, mFbo->getHeight()*2);

		{
			gl::ScopedFramebuffer fbScp(tmpFbo);
			gl::pushViewport( ivec2( 0, 0 ), tmpFbo->getSize() );
			// we need to save the current FBO because we'll be calling bindFramebufferFace() below
			gl::context()->pushFramebuffer();

			{
				gl::setMatricesWindow( tmpFbo->getWidth(), tmpFbo->getHeight() );
				gl::ScopedDepth d( false );

				gl::drawEquirectangular( mFbo->getTextureCubeMap(), Rectf( 0, 0, tmpFbo->getWidth(), tmpFbo->getHeight() ) ); // try this alternative

				// restore the FBO before we bound the various faces of the CubeMapFbo
				gl::context()->popFramebuffer();
				gl::popViewport();
			}
		}

		writeImage( writeFile(path), tmpFbo->getColorTexture()->createSource() );
	}

public:
	ci::signals::Signal<void()> drawSignal;

private:
	gl::FboCubeMapRef		mFbo;
};

class DynamicCubeMappingApp : public App {
  public:
	void setup() override;
	void resize() override;
	void update() override;
	void keyDown( KeyEvent event ) override;

	void drawSatellites();
	void drawSkyBox();
	void draw() override;

	gl::TextureCubeMapRef	mSkyBoxCubeMap,mSkyBoxCubeMap2;
	gl::BatchRef			mTeapotBatch, mSkyBoxBatch;
	mat4					mObjectRotation;
	CameraPersp				mCam;
	CubeMapperRef cubeMapperRef;

	bool					mDrawCubeMap;

	std::vector<Satellite>	mSatellites;
	gl::BatchRef			mSatelliteBatch;

	bool bToggle=false;
	bool bSky=true;
	bool bBalls=true;
	bool bTeapot=true;

	bool bSky2=true;
	bool bBalls2=true;

	bool bColored=false;
};

const int SKY_BOX_SIZE = 500;

void DynamicCubeMappingApp::setup()
{
	mSkyBoxCubeMap = gl::TextureCubeMap::create( loadImage( loadAsset( "env_map.jpg" ) ), gl::TextureCubeMap::Format().mipmap() );
	mSkyBoxCubeMap2 = gl::TextureCubeMap::create( loadImage( loadAsset( "env_map_color.jpg" ) ), gl::TextureCubeMap::Format().mipmap() );

#if defined( CINDER_GL_ES )
	auto envMapGlsl = gl::GlslProg::create( loadAsset( "env_map_es2.vert" ), loadAsset( "env_map_es2.frag" ) );
	auto skyBoxGlsl = gl::GlslProg::create( loadAsset( "sky_box_es2.vert" ), loadAsset( "sky_box_es2.frag" ) );
#else
	auto envMapGlsl = gl::GlslProg::create( loadAsset( "env_map.vert" ), loadAsset( "env_map.frag" ) );
	auto skyBoxGlsl = gl::GlslProg::create( loadAsset( "sky_box.vert" ), loadAsset( "sky_box.frag" ) );
#endif

	mTeapotBatch = gl::Batch::create( geom::Teapot().subdivisions( 7 ), envMapGlsl );
	mTeapotBatch->getGlslProg()->uniform( "uCubeMapTex", 0 );

	mSkyBoxBatch = gl::Batch::create( geom::Cube(), skyBoxGlsl );
	mSkyBoxBatch->getGlslProg()->uniform( "uCubeMapTex", 0 );

	// setup satellites (orbiting spheres )
	for( int i = 0; i < 33; ++i ) {
		mSatellites.push_back( Satellite() );
		float angle = i / 33.0f;
		mSatellites.back().mColor = Colorf( CM_HSV, angle, 1.0f, 1.0f );
		mSatellites.back().mPos = vec3( cos( angle * 2 * M_PI ) * 7, 0, sin( angle * 2 * M_PI ) * 7 );
	}
	mSatelliteBatch = gl::Batch::create( geom::Sphere(), getStockShader( gl::ShaderDef().color() ) );

	mDrawCubeMap = true;

	cubeMapperRef = CubeMapper::create(1024,1024);
	cubeMapperRef->drawSignal.connect([this](){
		gl::clear();
		if(this->bBalls2)
			this->drawSatellites();
		if(this->bSky2)
			this->drawSkyBox();
	});

	gl::enableDepthRead();
	gl::enableDepthWrite();
}

void DynamicCubeMappingApp::resize()
{
	mCam.setPerspective( 60, getWindowAspectRatio(), 1, 1000 );
}

void DynamicCubeMappingApp::update()
{
	// move the camera semi-randomly around based on time
	mCam.lookAt( vec3( 8 * sin( getElapsedSeconds() / 1 + 10 ), 7 * sin( getElapsedSeconds() / 2 ), 8 * cos( getElapsedSeconds() / 4 + 11 ) ), vec3( 0 ) );

	// rotate the object (teapot) a bit each frame
	mObjectRotation *= rotate( 0.04f, normalize( vec3( 0.1f, 1, 0.1f ) ) );

	// move the satellites
	for( int i = 0; i < 33; ++i ) {
		float angle = i / 33.0f;
		mSatellites[i].mPos = vec3( cos( angle * 2 * M_PI ) * 7, 6 * sin( getElapsedSeconds() * 2 + angle * 4 * M_PI ), sin( angle * 2 * M_PI ) * 7 );
	}
}

void DynamicCubeMappingApp::keyDown( KeyEvent event )
{
	if( event.getChar() == 'd' )
		mDrawCubeMap = ! mDrawCubeMap;

	if( event.getChar() == '/' )
		bToggle = !bToggle;

	if( event.getChar() == 's' )
		bSky = !bSky;

	if( event.getChar() == 'S' )
		bSky2 = !bSky2;

	if( event.getChar() == 't' )
		bTeapot = !bTeapot;

	if( event.getChar() == 'b' )
		bBalls=!bBalls;

	if( event.getChar() == 'B' )
		bBalls2=!bBalls2;

	if(event.getChar() == 'c')
		bColored = !bColored;

	if( event.getChar() == 'e' )
		cubeMapperRef->writeImageEquirectangular("export-equirectangular.png");

	if( event.getChar() == 'E' )
		cubeMapperRef->writeImageHorizontalCross("export-cross.png");

}

void DynamicCubeMappingApp::drawSatellites()
{
	for( const auto &satellite : mSatellites ) {
		gl::pushModelMatrix();
		gl::translate( satellite.mPos );
		gl::color( satellite.mColor );
		mSatelliteBatch->draw();
		gl::popModelMatrix();
	}
}

void DynamicCubeMappingApp::drawSkyBox()
{
	(bColored ? mSkyBoxCubeMap2 : mSkyBoxCubeMap)->bind();
	gl::pushMatrices();
		gl::scale( SKY_BOX_SIZE, SKY_BOX_SIZE, SKY_BOX_SIZE );
		mSkyBoxBatch->draw();
	gl::popMatrices();
}

void DynamicCubeMappingApp::draw()
{
	cubeMapperRef->update();

	gl::clear( Color( 0, 0, 0 ) );
	gl::setMatrices( mCam );

	// now draw the full scene
	if(bBalls)
		drawSatellites();

	if(bSky)
		drawSkyBox();

	if(bTeapot){
		// use cubemapper for teapot reflection
		cubeMapperRef->getFboCubeMap()->bindTexture( 0 );
		gl::pushMatrices();
			gl::multModelMatrix( mObjectRotation );
			gl::scale( vec3( 4 ) );
			mTeapotBatch->draw();
		gl::popMatrices();
	}

	// draw flat preview of cubemap texture
	if( mDrawCubeMap ) {
		gl::setMatricesWindow( getWindowSize() );
		gl::ScopedDepth d( false );
		//gl::drawHorizontalCross( cubeMapperRef->getFboCubeMap()->getTextureCubeMap(), Rectf( 0, 0, 300, 150 ) );

		if(bToggle)
			gl::drawHorizontalCross( cubeMapperRef->getFboCubeMap()->getTextureCubeMap(), Rectf( 0, 0, 300, 150 ) );
		else
			gl::drawEquirectangular( cubeMapperRef->getFboCubeMap()->getTextureCubeMap(), Rectf( 0, getWindowHeight() - 200, 400, getWindowHeight() ) ); // try this alternative
	}
}

CINDER_APP( DynamicCubeMappingApp, RendererGl( RendererGl::Options().msaa( 16 ) ) )
