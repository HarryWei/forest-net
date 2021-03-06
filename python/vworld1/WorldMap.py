import sys

MAX_VIS = 20

class WorldMap :
	""" Maintain map of the virtual world.

	This object maintains a map of a simple virtual world.
	It defines regions where avatars can walk and regions where they can't.
	It also defines the visibility of different regions using "walls".

	The model is vey simple. The world is divided up into a grid of
	squares, which are walkable or not. Each square can also have a
	wall on its west edge or north edge. If two points are separated
	by a wall, there is no visibility between those two points.
	"""
	def __init__(self) :
		""" Initialize a new WorldMap object.
		"""
		pass

	def init(self, mapFile) :
		f = None
		try :
			f = open(mapFile, 'r')
		except IOError as e :
			sys.stderr.write("WorldMap: cannot open " + mapFile)
			return False

		# The map file is a simple text file that defines map
		# Walls are shown as horizontal or vertical lines.
		# Unwalkable squares are marked with an x.
		# Each square occupies two columns and two rows in the file.
		# The internal data structure is a linear list, with xy
		# coordinates mapping to a "cell index"
		lines = f.readlines()
		self.size = len(lines)/2
		y = self.size - 1
		self.map = [0] * (self.size*self.size)
		horizRow = True
		for line in lines :
			if len(line) != 1+len(lines) :
				sys.stderr.write(str(len(line)) + " " + \
						 str(len(lines)) + "\n")
				sys.stderr.write("Core.setupWorld: length of " \
					"line " + str(self.size-y) + \
					" does not match line count\n")
				return False
			for xx in range(0,2*self.size) :
				pos = y * self.size+ xx/2
				if horizRow :
					if not xx&1 : continue
					if line[xx] == '-' :
						self.map[pos] |= 2
					continue
				if xx&1 : 
					if line[xx] == 'x' :
						self.map[pos] |= 4
				else :
					if line[xx] == '|' :
						self.map[pos] |= 1
			horizRow = not horizRow;
			if horizRow : y -= 1
		return True
	
	def blocked(self, c) :
		""" Determine if a cell is blocked (not walkable).
		
		c is the index of a cell
		return True if c is blocked, else false
		"""
		return self.map[c] & 4

	def separated(self,c0,c1) :
		""" Determine if two adjacent cells are separated by a wall.

		c0 is index of some cell
		c1 is index of an adjacent cell
		"""
		if c0 > c1 : c0, c1 = c1, c0
		if c0/self.size == c1/self.size : # same row
			return self.map[c1] & 1
		elif c0%self.size == c1%self.size : # same column
			return self.map[c0] & 2
		elif c0%self.size > c1%self.size : # se/nw diagonal
			if self.map[c0]&3 == 3 or \
			   (self.map[c0]&1 and self.map[c1+1]&1) or \
			   (self.map[c0]&2 and self.map[c0-1]&2) or \
			   (self.map[c1+1]&1 and self.map[c0-1]&2) :
				return True
			else :
				return False
		else : # sw/ne diagonal
			if (self.map[c0]&2 and self.map[c0+1]&1) or \
			   (self.map[c0+1]&1 and self.map[c1]&1) or \
			   (self.map[c0]&2 and self.map[c0+1]&2) or \
			   (self.map[c0+1]&2 and self.map[c1]&1) :
				return True
			else :
				return False

	def computeVisSet(self,c1) :
		""" Compute a visibility set for a square in the virtual world.

			c1 is cell number for a square in virtual world
	 	"""
		x1 = c1%self.size; y1 = c1/self.size;
		vSet = set();
		vSet.add(c1)
	
		visVec = [False] * self.size
		prevVisVec = [False] * self.size
	
		# start with upper right quadrant
		prevVisVec[0] = True
		dlimit = 1 + min(self.size,MAX_VIS);
		for d in range(1,dlimit) :
			done = True
			for x2 in range(x1,min(x1+d+1,self.size)) :
				dx = x2 - x1; y2 = d + y1 - dx 
				if y2 >= self.size : continue
				visVec[dx] = False
				if (x1==x2 and not prevVisVec[dx]) or \
				   (x1!=x2 and y1==y2 and \
						not prevVisVec[dx-1]) \
				   or (x1!=x2 and y1!=y2 and \
						not prevVisVec[dx-1] \
				   and not prevVisVec[dx]) :
					continue
				c2 = x2 + y2*self.size
	
				if self.isVis(c1,c2) :
					visVec[dx] = True
					vSet.add(c2)
					done = False
			if done : break
			for x2 in range(x1,min(x1+d+1,self.size)) :
				prevVisVec[x2-x1] = visVec[x2-x1]
		# now process upper left quadrant
		prevVisVec[0] = True;
		for d in range(1,dlimit) :
			done = True;
			for x2 in range(max(x1-d,0),x1+1) :
				dx = x1-x2; y2 = d + y1 - dx
				if y2 >= self.size : continue
				visVec[dx] = False;
				if (x1==x2 and not prevVisVec[dx]) or \
				   (x1!=x2 and y1==y2 and \
						not prevVisVec[dx-1]) or \
				   (x1!=x2 and y1!=y2 and \
						not prevVisVec[dx-1] and \
						not prevVisVec[dx]) :
					continue
				c2 = x2 + y2*self.size;
				if self.isVis(c1,c2) :
					visVec[dx] = True
					vSet.add(c2)
					done = False
			if done : break
			for x2 in range(max(x1-d,0),x1+1) :
				dx = x1-x2; prevVisVec[dx] = visVec[dx]
		# now process lower left quadrant
		prevVisVec[0] = True;
		for d in range(1,dlimit) :
			done = True;
			for x2 in range(max(x1-d,0),x1+1) :
				dx = x1-x2; y2 = (y1 - d) + dx
				if y2 < 0 : continue
				visVec[dx] = False
				if (x1==x2 and not prevVisVec[dx]) or \
				   (x1!=x2 and y1==y2 and \
						not prevVisVec[dx-1]) or \
				   (x1!=x2 and y1!=y2 and \
						not prevVisVec[dx-1] and \
						not prevVisVec[dx]) :
					continue
				c2 = x2 + y2*self.size;
				if self.isVis(c1,c2) :
					visVec[dx] = True
					vSet.add(c2)
					done = False
			if done : break
			for x2 in range(max(x1-d,0),x1+1) :
				dx = x1-x2; prevVisVec[dx] = visVec[dx]
		# finally, process lower right quadrant
		prevVisVec[0] = True;
		for d in range(1,dlimit) :
			done = True;
			for x2 in range(x1,min(x1+d+1,self.size)) :
				dx = x2-x1; y2 = (y1 - d) + dx
				if y2 < 0 : continue
				visVec[dx] = False
				if (x1==x2 and not prevVisVec[dx]) or \
				   (x1!=x2 and y1==y2 and \
						not prevVisVec[dx-1]) or \
				   (x1!=x2 and y1!=y2 and \
						not prevVisVec[dx-1] and \
						not prevVisVec[dx]) :
					continue
				c2 = x2 + y2*self.size
				if self.isVis(c1,c2) :
					visVec[dx] = True
					vSet.add(c2)
					done = False
			if done : break
			for x2 in range(x1,min(x1+d+1,self.size)) :
				dx = x2-x1; prevVisVec[dx] = visVec[dx]
		return vSet

	def isVis(self, c1, c2) :
		""" Determine if two squares are visible from each other.

	   	c1 is the cell number of the first square
	   	c2 is the cell number of the second square
		"""
		i1 = c1%self.size; j1 = c1/self.size;
		i2 = c2%self.size; j2 = c2/self.size;

		eps = .01
		points1 = ((i1+eps,  j1+eps),   (i1+eps,  j1+1-eps),
			   (i1+1-eps,j1+eps),   (i1+1-eps,j1+1-eps))
		points2 = ((i2+eps,  j2+eps),   (i2+eps,  j2+1-eps),
			   (i2+1-eps,j2+eps),   (i2+1-eps,j2+1-eps))

		# Check sightlines between all pairs of "corners"
		for p1 in points1 :
			for p2 in points2 :
				if self.canSee(p1,p2) : return True
		return False

	def canSee(self,p1,p2) :
		""" Determine if two points are visible to each other.

		p1 and p2 are (x,y) pairs representing points in the world
		"""
		if p1[0] > p2[0] : p1,p2 = p2,p1

		i1 = int(p1[0]); j1 = int(p1[1])
		i2 = int(p2[0]); j2 = int(p2[1])

		if i1 == i2 :
			minj = min(j1,j2); maxj = max(j1,j2)
			for j in range(minj,maxj) :
				if self.map[i1+j*self.size]&2 :
					return False
			return True

		slope = (p2[1]-p1[1])/(p2[0]-p1[0])

		i = i1; j = j1;
		x = p1[0]; y = p1[1]
		xd = i1+1 - x; yd = xd*slope
		c = i+j*self.size
		while i < i2 :
			if slope > 0 :
				while j+1 <= y+yd :
					#print "j=",j
					if self.map[c]&2 : return False
					j += 1; c += self.size
			else :
				while j >= y+yd :
					j -= 1; c -= self.size
					if self.map[c]&2 : return False
			c += 1
			if self.map[c]&1 : return False
			if slope > 0 :
				ylim = min(y+slope, p2[1])
				while j+1 <= ylim :
					if self.map[c]&2 : return False
					j += 1; c += self.size
			else :
				ylim = max(y+slope, p2[1])
				while j >= ylim :
					j -= 1; c -= self.size
					if self.map[c]&2 : return False
			i += 1; x += 1.0; y += slope
		return True
