def parse(text):
	stack = []
	nodes = []
	node = ''
	string = False
	for character in text:
		if string:
			if character == '"':
				string = False
				node += character
				# nodes.append(node)
			else:
				node += character
		elif character == '"':
			string = True
			node += character
		elif character not in ['(', ')', ' ', '\t', '\r', '\n']:
			node += character
		else:
			if node != '':
				nodes.append(node)
			node = ''
			if character == '(':
				stack.append(nodes)
				nodes = []
			elif character == ')':
				parent = stack.pop()
				parent.append(nodes)
				nodes = parent
		# print("d -  ", character, node, nodes)
	if (len(stack) != 0):
		raise Exception('Mismatching brackets')
	return nodes

def unparse(subject):
	if isinstance(subject, list):
		return '(' + ' '.join([unparse(node) for node in subject]) + ')\n'
	elif len(subject) == 0 \
			or '(' in subject \
			or ')' in subject \
			or ' ' in subject \
			or '\t' in subject \
			or '\r' in subject \
			or '\n' in subject:
		# print(subject)
		if len(subject) > 1:
			if subject[0] == '"':
				return subject
		return '"' + subject + '"'
	else:
		# print(subject)
		return subject


def matchingNode(node, matchingValues):
    for i in range(len(matchingValues)):
        if node[i] != matchingValues[i]:
            return False
    return True

def findNode(nodes, matchingValues):
    for i in len(nodes):
        if isinstance(node[i], list) and self._matchingNode(node[i], matchingValues):
            return i
    Exception("Failed to find node with values:" + matchingValues)

# unparsed = unparse(
# 	parse(
# 		open('example.kicad_pcb').read()
# 	)[0]
# )
# print(unparsed)
