
GLsizei spriteWidth  = 3;
GLsizei spriteHeight = 3;
GLsizei spriteDepth  = 4;

GLubyte spriteData[spriteWidth][spriteHeight][spriteDepth] = {
  {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
  {{1,1,1,1}, {1,1,1,1}, {1,1,1,1}},
  {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}}
}

GLuint spriteTextureID;

void
initializeSpriteTexture()
{
	glEnable (GL_TEXTURE_2D);
  glEnable (GL_POINT_SPRITE_ARB);
	glGenTextures (1, &spriteTextureID);
	glBindTexture (GL_TEXTURE_2D, spriteTextureID);
	gluBuild2DMipmaps (GL_TEXTURE_2D, 4, spriteWidth, spriteHeight, GL_RGBA, GL_UNSIGNED_BYTE, spriteData);
	glTexParameteri (GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri (GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexEnvf (GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE );
  glBindTexture (GL_TEXTURE_2D, spriteTextureID);
}
