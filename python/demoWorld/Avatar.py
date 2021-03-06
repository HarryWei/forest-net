""" Avatar - script that displays Avatar's view of world and controls movement

Author: Chao Wang and Jon Turner
World Model: Chao Wang
 
Adapted from "Roaming Ralph", a tutorial included in Panda3D package.
Author: Ryan Myers
Models for Ralph: Jeff Styers, Reagan Heller
"""  

import random, sys, os, math, re
from threading import Thread, Lock
import time

import direct.directbase.DirectStart
from panda3d.core import *
from direct.gui.OnscreenText import OnscreenText
from direct.gui.OnscreenImage import OnscreenImage
from direct.actor.Actor import Actor
from direct.showbase.DirectObject import DirectObject
from direct.interval.IntervalGlobal import Sequence
from panda3d.core import Point3
from pandac.PandaModules import CardMaker

import Util
from Util import *
import Places

if Util.AUDIO : import pyaudio; import wave

def addTitle(text):
	""" Show a title on the screen at bottom right.
	"""
	return OnscreenText(text=text, style=1, fg=(1,1,1,1), \
		pos=(1.3,-0.95), align=TextNode.ARight, scale = .07)

def showText(pos):
	""" Show text on the screen at a specified position.
	"""
	return OnscreenText( \
		text=" ", \
		style=1, fg=(0,0,0,1), pos=(-1.3, pos), \
		align=TextNode.ALeft, scale = .06, mayChange = True)

class Avatar(DirectObject):
	def __init__(self, userName, avaModel):
		self.userName = userName
		self.avaModel = avaModel

		print(cpMgr)
		#cvMgr.listVariables()
		#clock-mode.setValue("limited")
		#clock-frame-rate.setValue(20)

		# Setup graphics
		if Util.SHOW :
			base.windowType = 'onscreen' 
			wp = WindowProperties.getDefault() 
			base.openDefaultWindow(props = wp)
			base.win.setClearColor(Vec4(0,0,0,1))
		
		# Set up the environment
		self.environ = loader.loadModel("models/map")
		self.environ.reparentTo(render)
		self.environ.setPos(0,0,0)

		self.setupLimits()
		self.setupAvatar()
		if Util.AUTO :
			self.setupAuto()
		else :
			self.setupKeyMappings()
		if Util.SHOW or (not Util.NOSUB) :
			self.setupCollisions()
		if Util.SHOW :
			self.setupRemotes()
			base.setBackgroundColor(0.5,0.7,1,1)
			self.title = addTitle("demo world")
			self.setupScreenText()
			self.setupMap()
			self.setupCamera()
			self.setupLights()
		if Util.AUDIO : self.setupAudio()

		# add move method to task list
		taskMgr.add(self.move,"moveTask")

	def setupLimits(self) :
		minBounds, maxBounds = self.environ.getTightBounds()
		size = maxBounds - minBounds
		self.modelSizeX = size.getX()
		self.modelSizeY = size.getY()

	def setupAuto(self) :
		""" Initialize variables that control automatic wandering.
		"""
		self.rotateDir = -1
		self.rotateDuration = -1
		self.moveDir = -1
		self.moveDuration = -1
		self.isAvoidingCollision = False
		self.inBigRotate = False # if True, do not move forward;
					 # only rotate
		return

	def setupScreenText(self) :
		""" Setup screen text that will appear at upper right.
		"""
		# Create object to show avatar's position on the screen.
		# Update actual text using setText method on object.
		self.avPos = showText(0.92)

 		# Create object to show a list of visible avatars
 		self.showNumVisible = showText(0.85)
 		self.visList = []

		# Create object for displaying keyboard shortcuts
		self.helpText = showText(0.78)
		self.helpText.setText("h: for help")

	def setupAvatar(self) :
		""" Load avatar model and add to scene graph.
		"""
		self.avatarNP = NodePath("ourAvatarNP")
		self.avatarNP.reparentTo(render)
		if Util.AUTO :
			p = random.randint(1,Places.getNumPlaces())
			nbors = Places.getNeighbors(p)
			self.avatarNP.setPos(Places.getLoc(p)[0],
					     Places.getLoc(p)[1],0)
			t = nbors[random.randint(0,len(nbors)-1)]
			self.avatarNP.setH(self.heading(
				self.avatarNP.getX(), self.avatarNP.getY(),
				Places.getLoc(t)[0], Places.getLoc(t)[1]))
			self.oldPlace = p
			self.targetPlace = t
			self.turning = False
			self.deltaH = 0
		else :
			self.avatarNP.setPos(63,84,0)
			self.avatarNP.setH(0)

		s = str(self.avaModel)
		self.avatar = Actor("models/ava" + s,{"walk":"models/walk" + s})
		self.avatar.reparentTo(self.avatarNP)

		if   self.avaModel == 1 : self.avatar.setScale(.002)
		elif self.avaModel == 2 : self.avatar.setScale(.3)
		elif self.avaModel == 3 : self.avatar.setScale(1)
		elif self.avaModel == 4 : self.avatar.setScale(.9)
		self.avatar.setPos(0,0,0)
		self.avatar.setH(0)

	def setupCamera(self) :
		""" Setup the camera, relative to avatar.
		"""
		base.disableMouse()
		base.camera.setPos(self.avatarNP.getPos())
		base.camera.setZ(self.avatarNP.getZ()+1.5)
		base.camera.setHpr(self.avatarNP.getHpr()[0],0,0)		
		self.fieldAngle = 46.8	# similar to human eye;
					# change this to zoom in/out
		base.camLens.setFov(self.fieldAngle)		
			
	def setupMap(self) :
		""" Add overview map and highlight position.
		"""
		self.Dmap = OnscreenImage(image = 'models/mapTopView.png', \
					  #pos = (.8,0,.6), scale = .4)
					  pos = (0.8,0,0.6), scale = .4)
		self.Dmap.setTransparency(TransparencyAttrib.MAlpha)
		self.dot = OnscreenImage(image = 'models/dot.png', \
					 pos = (1,0,1), scale = .01)

		# Set the dot's position in the 2d map
		#self.dot.setPos(0,0,0)
#		  0.0+self.Dmap.getX(),0, \
#		  0.0+self.Dmap.getY())
	#	  self.avatarNP.getX()/(self.modelSizeX+0.0+self.Dmap.getX()),0, \
	#	  self.avatarNP.getY()/(self.modelSizeY+0.0+self.Dmap.getY()))
		self.dot.setPos( \
		  (self.avatarNP.getX()/(self.modelSizeX))*0.79+0.4, 0, \
		  (self.avatarNP.getY()/(self.modelSizeY))*0.79+0.21)
		self.dotOrigin = self.dot.getPos()

	def setupCollisions(self) :
 		""" Setup collision solids.

		These serve two purposes
 		1. prevent the avatar from passing through a wall
		2. keep the avatar on the ground (we skip it for now since we have flat plane)
		"""
		# Setup collisionBitMasks
		WALL_MASK = BitMask32.bit(2)
		self.buildingCollider = self.environ.find("**/SketchUp.002")
		self.buildingCollider.node().setIntoCollideMask(WALL_MASK)
	#	self.buildingCollider.show()

		base.cTrav = CollisionTraverser()
		base.cTrav.setRespectPrevTransform(True)

		self.avatarSpNp = self.avatarNP.attachNewNode( \
					CollisionNode('avatarSphere'))
		
		self.csSphere = self.avatarSpNp.node().addSolid(
			CollisionSphere(0.0,-0.5,0.9,0.9))
		# format above: (x,y,z,radius)

		self.avatarSpNp.node().setFromCollideMask(WALL_MASK)
		self.avatarSpNp.node().setIntoCollideMask(BitMask32.allOff())

		# uncomment the following line to show the collisionSphere
	#	self.avatarSpNp.show()

		self.avatarGroundHandler = CollisionHandlerPusher()
		self.avatarGroundHandler.setHorizontal(True)
		self.avatarGroundHandler.addCollider(self.avatarSpNp, \
					self.avatarNP)
		base.cTrav.addCollider(self.avatarSpNp,\
					self.avatarGroundHandler)

		# setup a collision solid for the canSee method
		self.auxCSNp = self.environ.attachNewNode(CollisionNode( \
						'csForCanSee'))
		self.auxCSSolid = self.auxCSNp.node().addSolid( \
						CollisionSegment())
		self.auxCSNp.node().setFromCollideMask(WALL_MASK)
		self.auxCSNp.node().setIntoCollideMask(BitMask32.allOff())

		self.csHandler = CollisionHandlerQueue()

		self.csTrav = CollisionTraverser('CustomTraverser')
		self.csTrav.addCollider(self.auxCSNp, self.csHandler)

		# uncomment the following line to show where collision occurs
		# self.csTrav.showCollisions(render)

	def setupLights(self) :
		""" Setup lighting for the scene.
		"""
		self.ambientLight = render.attachNewNode(AmbientLight( \
					"ambientLight"))
		self.ambientLight.node().setColor(Vec4(.8,.8,.8,1))
		render.setLight(self.ambientLight)

		dLight1 = DirectionalLight("dLight1")
		dLight1.setColor(Vec4(6,5,7,1))
		dLight1.setDirection(Vec3(1,1,1))
		dlnp1 = render.attachNewNode(dLight1)
		dlnp1.setHpr(30,-160,0)
		render.setLight(dlnp1)

		dLight2 = DirectionalLight("dLight2")
		dLight2.setColor(Vec4(.6,.7,1,1))
		dLight2.setDirection(Vec3(-1,-1,-1))
		self.dlnp2 = render.attachNewNode(dLight2)
		self.dlnp2.node().setScene(render)
		self.dlnp2.setHpr(-70,-60,0)
		render.setLight(self.dlnp2)

	def setupKeyMappings(self) :
		""" Setup key mappings for controlling avatar.
		"""
		# first create keyMap object with default values
		self.keyMap = { "left":0, "right":0, \
				"forward":0, "backward":0, "dash":0, \
				"slide-left":0, "slide-right":0, \
 				"cam-up":0, "cam-down":0, \
				"cam-left":0, "cam-right":0, \
				"zoom-in":0, "zoom-out":0, \
				"reset-view":0, "view":0}
		
		# now setup keyboard events that modify keyMap thru setKey
		self.accept("escape", sys.exit)

		# turn help text on/off
		self.accept("h", self.setKey, ["help",1])
		self.accept("h-up", self.setKey, ["help",0])

		# movement controls
		self.accept("arrow_left", self.setKey, ["left",1])
		self.accept("arrow_left-up", self.setKey, ["left",0])
		self.accept("arrow_right", self.setKey, ["right",1])
		self.accept("arrow_right-up", self.setKey, ["right",0])

		self.accept("arrow_up", self.setKey, ["forward",1])
		self.accept("arrow_up-up", self.setKey, ["forward",0])
 		self.accept("arrow_down", self.setKey, ["backward",1])
 		self.accept("arrow_down-up", self.setKey, ["backward",0])

 		self.accept(",", self.setKey, ["slide-left",1])
 		self.accept(",-up", self.setKey, ["slide-left",0])
 		self.accept(".", self.setKey, ["slide-right",1])
 		self.accept(".-up", self.setKey, ["slide-right",0])

		self.accept("alt-arrow_up", self.setKey, ["dash", 1])
 		self.accept("alt-up", self.setKey, ["dash", 0])

		# camera direction contols
		self.accept("shift-arrow_up", self.setKey, ["cam-up",1])
		self.accept("shift-arrow_down", self.setKey, ["cam-down",1])
		self.accept("shift-arrow_left", self.setKey, ["cam-left",1])
		self.accept("shift-arrow_right", self.setKey, ["cam-right",1])	

		# zoom controls
		self.accept("z", self.setKey, ["zoom-in",1])
		self.accept("z-up", self.setKey, ["zoom-in",0])
 		self.accept("shift-z", self.setKey, ["zoom-out",1])
		self.accept("r", self.setKey, ["reset-view",1])         
		self.accept("r-up", self.setKey, ["reset-view",0])         

		self.accept("v", self.setKey, ["view",1])
		self.accept("v-up", self.setKey, ["view",0])

#		self.accept("mouse1", self.calling)
#		self.accept("mouse2", self.showPic)
#		self.accept("mouse3", self.teleport)
#		self.accept("f1", self.showInfo)
#		self.accept("m", self.mute)
#		self.accept("s", self.switchRegion)
#		self.accept("control-s", self.switchView)

	def setKey(self, key, value):
		""" Records the state of the arrow keys
		"""
		self.keyMap[key] = value

		if key == "help" :
			if value == 1 :
				self.helpText.setText( \
					"arrows to move or turn\n" + \
					"shift-arrows to change view\n" + \
					"z/Z to zoom in/out, r to reset\n" + \
					",/. to slide left/right")
			else :
				self.helpText.setText("h for help")

		if value == 1 : return

		# special cases for releasing keys with modifiers
		if key == "zoom-in" :
			self.keyMap["zoom-out"] = 0
		if key == "left" or key == "right" :
			self.keyMap["cam-left"] = 0
			self.keyMap["cam-right"] = 0
		if key == "forward" or key == "backward" :
			self.keyMap["cam-up"] = 0
			self.keyMap["cam-down"] = 0
	
	def setupAudio(self) :
		# set up the audio
		self.audioLevel = 0  	# audio volume
		self.audioData = ''	# audio samples from microphone	 
		self.isListening = False
		if Util.AUDIO :
			p = pyaudio.PyAudio()
			print p.get_device_info_by_index(0)['defaultSampleRate']

	def setupRemotes(self) :
		# Setup data for controlling remote avatars - key is remote's id
		self.remoteMap = {}	# map of potentially visible remotes
		self.remoteNames = {}	# user names of thedr remotes
		self.remotePics = {}	# picture files
		self.actorCache = []	# cache of unused Actor objects
					# indexed by mode1 number (1..1000)
		for i in range(0,1001) :
			self.actorCache.append([])

	def getActor(self, avaNum) :
		""" Get an Actor object using a specfied model.

		We first check the actorCache, and if no model is currently
		available, we create one.
		avaNum is the avatar serial number for the desired Actor
		"""
		actor = None
		if len(self.actorCache[avaNum]) > 0 :
			actor = self.actorCache[avaNum].pop()
		else :
			s = str(avaNum)
			actor = Actor("models/ava" + s, \
				      {"walk":"models/walk" + s})
			if   avaNum == 1 : actor.setScale(.002)
			elif avaNum == 2 : actor.setScale(.3)
			elif avaNum == 3 : actor.setScale(1)
			elif avaNum == 4 : actor.setScale(.9)
		return actor

	def recycleActor(self, actor, avaNum) :
		""" Recycle an Actor object.

		Just put it back in the cache.
		actor is an Actor object that is not currently needed
		avaNum is the serial number for the actor's model
		"""
		self.actorCache[avaNum].append(actor)
		
	def addRemote(self, x, y, z, direction, id, name, avaNum) : 
		""" Add a remote avatar.

		This method is used by the Net object when it starts
		getting packets from an avatar that it did not previously
		know about.
		x,y gives the remote's position
		direction gives its heading
		id is an identifier used to distinguish this remote from others
		name is the user name of the remote
		avaNum is the serial number for this avatar's model

		New remotes are included in the scene graph.
		"""
		if not Util.SHOW : return
		if id in self.remoteMap:
			self.updateRemote(x,y,direction,id,name); return
		remote = self.getActor(avaNum)

		self.remoteMap[id] = [remote, avaNum, False, \
				      OnscreenImage(image = 'models/dot1.png', \
				      pos = ( \
		  (self.avatarNP.getX()/(self.modelSizeX))*0.79+0.4, 0, \
		  (self.avatarNP.getY()/(self.modelSizeY))*0.79+0.21),
				      scale = .01) ]

		# set position and direction of remote and make it visible
		remote.reparentTo(render)
		remote.setX(x); remote.setY(y); remote.setZ(z) 
		remote.setHpr(direction,0,0) 
		remote.stop(); remote.pose("walk",5)
				
		# display name: convert 3D coordinates to 2D
		# point(x, y, 0) is the position of the Avatar in the 3D world
		# the 2D screen position is given by a2d
		
		p3 = base.cam.getRelativePoint(render, Point3(x,y,z))
		p2 = Point2()
		name = "" # blank for now
		if base.camLens.project(p3, p2):
			r2d = Point3(p2[0], 0, p2[1])
			a2d = aspect2d.getRelativePoint(render2d, r2d) 
			if id not in self.remoteNames.keys():
				self.remoteNames[id] = OnscreenText( \
					text = name, pos = a2d, scale = 0.04, \
					fg = (1, 1, 1 ,1), \
					shadow = (0, 0, 0, 0.5) )
		
		# if off view
		else:
			if id in self.remoteNames.keys():
				if self.remoteNames[id]:
					self.remoteNames[id].destroy()	 				
	def updateRemote(self, x, y, z, direction, id, name) :
		""" Update location of a remote avatar.

		This method is used by the Net object to update the location.
		x,y,z gives the remote's position
		direction gives its heading
		id is an identifier used to distinguish this remote from others
		name is the remote's user name
		"""
		if not Util.SHOW : return
		if id not in self.remoteMap : return
		remote, avaNum, isMoving, dot = self.remoteMap[id]

		if abs(x-remote.getX()) > .01 or abs(y-remote.getY()) > .01 or \
		   abs(direction-remote.getHpr()[0]) > .01 :
			if not isMoving :
				remote.loop("walk")
				self.remoteMap[id][2] = True
			remote.setHpr(direction,0,0) 
			remote.setX(x); remote.setY(y); remote.setZ(z)
		else :
			remote.stop(); remote.pose("walk",1)
			self.remoteMap[id][2] = False 
		
		p3 = base.cam.getRelativePoint(render, Point3(x,y,z))
		p2 = Point2()
		name = "" # blank for now
		if base.camLens.project(p3, p2) and id in self.visList:
			r2d = Point3(p2[0], 0, p2[1])
			a2d = aspect2d.getRelativePoint(render2d, r2d) 
			if id in self.remoteNames.keys():
				if self.remoteNames[id]:
					self.remoteNames[id].destroy()
			self.remoteNames[id] = OnscreenText(text = name, \
						pos = a2d, scale = 0.04, \
						fg = (1, 1, 1, 1), \
						shadow = (0, 0, 0, 0.5))

			if id in self.remotePics.keys():
				if self.remotePics[id]:
					try:
						self.remotePics[id].setPos( \
							a2d[0], 0, a2d[1]) 
					# if pic happens to have been removed 
					# by the user
					except Exception as e:
						pass
					
		# off screen, delete text obj. and the picture
		else:
			if id in self.remoteNames.keys():
				if self.remoteNames[id]:
					self.remoteNames[id].destroy()	
			if id in self.remotePics.keys():
				if self.remotePics[id]:
					self.remotePics[id].destroy()
					self.remotePics[id] = None


	def removeRemote(self, id) :
		""" Remove a remote when it goes out of range.
		
		id is the identifier for the remote
		"""
		if not Util.SHOW : return
		if id not in self.remoteMap : return
		self.remoteMap[id][3].destroy()
		remote = self.remoteMap[id][0]
		
		if id in self.remoteNames.keys():
			if self.remoteNames[id]:
				self.remoteNames[id].destroy()	
				
		if id in self.remotePics.keys():
			if self.remotePics[id]:
				self.remotePics[id].destroy()
				self.remotePics[id] = None
				
		remote.detachNode()
		avaNum = self.remoteMap[id][1]
		self.recycleActor(remote,avaNum)
		del self.remoteMap[id]
		if id in self.visList:
			self.visList.remove(id)

	def getPosHeading(self) :
		""" Get the position and heading.
	
		Intended for use by a Net object that must track avatar's 
		position return a tuple containing the x-coordinate,
		y-coordinate, heading.
		"""
		return (self.avatarNP.getX(), self.avatarNP.getY(), \
			self.avatarNP.getZ(), (self.avatarNP.getHpr()[0])%360)

	# to display/hide pictures when the user clicks on the avatar/pic
	def showPic(self):
		if not Util.SHOW : return
		x = y = 0
		if base.mouseWatcherNode.hasMouse():
			x=base.mouseWatcherNode.getMouseX()
			y=base.mouseWatcherNode.getMouseY()
			#print "has mouse @: " + str(x) + " " + str(y)
		responded = 0	
		for id in self.remoteNames.keys():
			if abs(self.remoteNames[id].getPos()[0]- x) < 0.1 and \
			   abs(self.remoteNames[id].getPos()[1] - y) < 0.1:
				# click occurs close enough to the name of the \
				# avatar, try to create pic 
				if id not in self.remotePics.keys() or \
				   (id in self.remotePics.keys() and \
				    self.remotePics[id] is None):
					name = self.remoteNames[id].getText()
					card = OnscreenImage(image =  \
						'models/Display/'+ name + \
						'.jpg', pos = (x, 0, y), \
						scale = (0.2, 1, 0.2))
					self.remotePics[id] = card
					responded = 1
		if responded is not 1:
			for id in self.remotePics.keys():
				if self.remotePics[id]:
					card = self.remotePics[id]
					if abs(self.remoteNames[id].getX()-x)<1\
					   and abs(self.remoteNames[id].getY()<y) < 1:
						# the user clicked on the card
						if card :
							card.destroy()
							self.remotePics[id] = None
			
	def playAudio(self,inputData) :
		if not Util.AUDIO : return
		self.stream.write(inputData)
		stream.stop_stream()
		stream.close()
		p1.terminate()
		
		wf = wave.open('models/Panda.wav', 'rb')

		data = wf.readframes(self.CHUNK)

		while data != '':
			p = pyaudio.PyAudio()

			stream = p.open(format=p.get_format_from_width( \
					wf.getsampwidth()),
					channels=wf.getnchannels(),
					rate=wf.getframerate(), output=True)

		    	stream.write(data)
		    	data = wf.readframes(self.CHUNK)

			stream.stop_stream()
			stream.close()

			p.terminate()

	def getLimit(self) :
		""" Get the limit on the xy coordinates.
		"""
		return (self.modelSizeX, self.modelSizeY)
	
	def getAudioLevel(self) :
		""" Get audioLevel to see if Avatar is talking
		"""
		if not Util.AUDIO : return
		return self.audioLevel

	def record(self) :
		if not Util.AUDIO : return
		try:
			self.streamin.read(CHUNK)
		except IOError:
			print 'warning:dropped frame'

	def getaudioData(self) :
		if not Util.AUDIO : return
		return self.audioData    

	def canSee(self, p1, p2):
		""" Test if two points are visible from one another
	
		It works by setting up a collisionSegment between the
		two positions. Return True if the segment didn't hit
		any from/into object in between.
	
		An alternative is to create a pool of rays etc. in
		Avatar class, like what we've done with remotes, and pass
		them here, so that we don't create objects on the fly. But
		for canSee(), the current version demands less computation
		resources.
		"""

		# lift two points a bit above the grouond to prevent the
		# collision ray from hitting the edge of shallow terrain;
		# also, put them at different level so that the ray has
		# nonzero length (a requirement for collisionSegment()).
		p1[2] += 1
		p2[2] += 0.9
		self.auxCSNp.node().modifySolid(self.auxCSSolid).setPointA(p1)
		self.auxCSNp.node().modifySolid(self.auxCSSolid).setPointB(p2)
	
		self.csTrav.traverse(render)
	
		return (self.csHandler.getNumEntries() == 0)

	def move(self, task):
		""" Update the position of this avatar.

		This method is called by the task scheduler on every frame.
		It responds to various keys to control the avatar and its
		viewpoint.
		"""

		self.moveMyAvatar()

		if not Util.SHOW : return task.cont

		self.updateView()

		self.updateScreenText()

		self.updateMap()

		return task.cont

	def moveMyAvatar(self) :
		""" Update the position of avatar.
		"""
		if Util.AUTO : self.autoMove(); return

		newH = self.avatarNP.getH()
		newY = 0
		newX = 0

		if self.keyMap["left"] :
			newH += 30 * globalClock.getDt()
		if self.keyMap["right"] :
			newH -= 30 * globalClock.getDt()
		if self.keyMap["forward"] :
			newY -= 2 * globalClock.getDt()
 		if self.keyMap["backward"] :
 			newY += 2 * globalClock.getDt()
 		if self.keyMap["dash"] :
 			newY -= 20 * globalClock.getDt()
		if self.keyMap["slide-left"] :
 			newX += 2 * globalClock.getDt()
		if self.keyMap["slide-right"] :
 			newX -= 2 * globalClock.getDt()

		self.avatarNP.setH(newH)
		self.avatarNP.setFluidX(self.avatarNP, newX)
		self.avatarNP.setFluidY(self.avatarNP, newY)

	def heading(self,x1,y1,x2,y2) :
		dx = x2 - x1; dy = y2 - y1
		hyp = math.sqrt(dx*dx + dy*dy)
		if hyp < .000001 : return 0
		h = math.asin(dy/hyp)*360/(2*3.14159)
		if dx < 0 : h = 180 - h
		return h + 90

	def autoMove(self) :
		""" Move the avatar randomly, without user input.
		"""

		dx = Places.getLoc(self.targetPlace)[0] - self.avatarNP.getX()
		dy = Places.getLoc(self.targetPlace)[1] - self.avatarNP.getY()
		dist = math.sqrt(dx*dx + dy*dy)
		h0 = self.avatarNP.getH()
		if dist < 4 :
			# pick new target and determine deltaH
			nbors = Places.getNeighbors(self.targetPlace)
			x = random.randint(0,len(nbors)-1)
			if nbors[x] == self.oldPlace :
				x = (1 if x == 0 else x-1)
			t = nbors[x]
			h = self.heading(
				self.avatarNP.getX(), self.avatarNP.getY(),
				Places.getLoc(t)[0], Places.getLoc(t)[1])
			self.deltaH = h - h0
			if self.deltaH > 180 : self.deltaH -= 360
			elif self.deltaH < -180 : self.deltaH += 360
			self.deltaH /= 2
			self.oldPlace = self.targetPlace
			self.targetPlace = t
			self.turning = True

		# adjust heading and position
		t = self.targetPlace
		h = self.heading(self.avatarNP.getX(), self.avatarNP.getY(),
				 Places.getLoc(t)[0], Places.getLoc(t)[1])
		dh1 = h - h0
		if dh1 > 180 : dh1 -= 360
		elif dh1 < -180 : dh1 += 360
		if self.turning :
			dh2 = self.deltaH * globalClock.getDt()
			if math.fabs(dh1) <= math.fabs(dh2) : 
				self.turning = False
			else :
				h = h0 + dh2
		self.avatarNP.setH(h)
		self.avatarNP.setFluidY(self.avatarNP,-2 * globalClock.getDt())
		
		return

		"""
		if self.rotateDir == -1:
			self.rotateDir = random.randint(1,25) #chances to rotate
		if self.rotateDuration == -1:
			self.rotateDuration = random.randint(200,400)

		# guide the moving direction of the bot
		if self.rotateDir <= 3 : # turn left
			self.avatarNP.setH(self.avatarNP.getH() + \
					 40 * globalClock.getDt())
			self.rotateDuration -= 1
			if self.rotateDuration <= 0:
				self.rotateDuration = -1
				self.rotateDir = -1
		elif self.rotateDir <= 6 : # turn right
			self.avatarNP.setH(self.avatarNP.getH() - \
					 50 * globalClock.getDt())
			self.rotateDuration -= 1
			if self.rotateDuration <= 0:
				self.rotateDuration = -1
				self.rotateDir = -1
		elif self.rotateDir == 7 : # turn big left
			self.avatarNP.setH(self.avatarNP.getH() + \
					 102 * globalClock.getDt())
			self.rotateDuration -= 1
			if self.rotateDuration <= 0:
				self.rotateDuration = -1
				self.rotateDir = -1
		elif self.rotateDir == 8 : # turn big right
			self.avatarNP.setH(self.avatarNP.getH() - \
					 102 * globalClock.getDt())
			self.rotateDuration -= 1
			if self.rotateDuration <= 0:
				self.rotateDuration = -1
				self.rotateDir = -1
		else :
			self.rotateDuration -= 1
			if self.rotateDuration <= 0:
				self.rotateDuration = -1
				self.rotateDir = -1
			self.avatarNP.setFluidPos(self.avatarNP, 0,
					-1 * globalClock.getDt(),
					self.avatarNP.getZ() )
		# moving forward
		#self.avatarNP.setFluidPos(self.avatarNP, 0,
	#				-1 * globalClock.getDt(),
	#				self.avatarNP.getZ() )
		return
		"""

	def updateView(self) :
		""" Update what appears on-screen in response to view controls.
		"""
		base.camera.setPos(self.avatarNP.getPos())
		# uncomment the following line for a third-persion view
	#	base.camera.setY(self.avatarNP.getY()+10)
		base.camera.setZ(self.avatarNP.getZ()+1.5)
		base.camera.setH(self.avatarNP.getH()+180)

		if Util.AUTO : return

		if self.keyMap["view"] :
			base.camera.setPos(self.avatarNP,
					   0,-3,self.avatarNP.getZ()+1.5)
			base.camera.setH(self.avatarNP.getH())
			return

		# Update where camera is looking
		camHeading = self.avatarNP.getH()+180
		if   self.keyMap["cam-left"] : camHeading += 40
		elif self.keyMap["cam-right"]: camHeading -= 40
		base.camera.setH(camHeading)

		camPitch = self.avatarNP.getP()
		if   self.keyMap["cam-up"] : camPitch = 20
		elif self.keyMap["cam-down"] : camPitch = -20
		base.camera.setP(camPitch)

		#hpr = self.avatarNP.getHpr()
		#pos = self.avatarNP.getPos()
		#hpr[0] = hpr[0] + 180 + camAngleX
		#hpr[1] = camAngle

		# update field-of-view in response to zoom controls
		if self.keyMap["zoom-in"] != 0 : 			
			if self.fieldAngle > 5 : self.fieldAngle *= .99
		elif self.keyMap["zoom-out"] != 0 :	
			if self.fieldAngle < 120 : self.fieldAngle *= 1.01
		elif self.keyMap["reset-view"] != 0 :
			self.fieldAngle = 46.8
		base.camLens.setFov(self.fieldAngle)		

	def updateScreenText(self) :
		""" Update informational text shown on the screen.
		"""
		# Update the text showing avatar's position
		h = self.avatarNP.getH()
		if h < -180 : h += 360
		elif h > 180 : h -= 360
		self.avPos.setText("Your location (x, y, dir): (%d, %d, %d)"%\
			(self.avatarNP.getX(), self.avatarNP.getY(), h))

		# update text showing visible avatars
 		self.showNumVisible.setText("Visible avatars: " + \
					    self.updateVisList())

	def updateVisList(self) :
		# Check visibility and update visList
		for id in self.remoteMap:
			if self.canSee(self.avatarNP.getPos(),
					self.remoteMap[id][0].getPos()) :
				if id not in self.visList :
					self.visList.append(id)
			elif id in self.visList:
				self.visList.remove(id)
		s = ""
		for id in self.visList : s += fadr2string(id) + " "
		return s

	def updateMap(self) :
		""" Show positions of this avatar and remotes as dots in map.
		"""
		self.dot.setPos( \
		  (self.avatarNP.getX()/(self.modelSizeX))*0.79+0.4, 0, \
		  (self.avatarNP.getY()/(self.modelSizeY))*0.79+0.21)
		for id in self.remoteMap:
			self.remoteMap[id][3].setPos( \
				(self.remoteMap[id][0].getX() / \
					self.modelSizeX)*0.79+0.4, \
				0, (self.remoteMap[id][0].getY() / \
					self.modelSizeY)*0.79+0.21)
