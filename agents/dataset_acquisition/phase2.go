package main

// RunPhase2STEM handles STEM and Social Sciences datasets
func RunPhase2STEM() error {
	var entries []DatasetEntry

	stemTexts := []string{
		"Psychology is the scientific study of mind and behavior. Psychology includes the study of conscious and unconscious phenomena, including feelings and thoughts.",
		"Sociology is a social science that focuses on society, human social behavior, patterns of social relationships, social interaction, and aspects of culture.",
		"Anthropology is the scientific study of humanity, concerned with human behavior, human biology, cultures, societies, and linguistics.",
	}
	for _, text := range stemTexts {
		entries = append(entries, DatasetEntry{Text: text, Source: "OpenSocialScience", Domain: "SocialSciences"})
	}

	philTexts := []string{
		"Logic is the study of correct reasoning or good arguments.",
		"Ethics is the branch of philosophy that involves systematizing, defending, and recommending concepts of right and wrong behavior.",
		"Metaphysics is the branch of philosophy that studies the fundamental nature of reality, the first principles of being, identity and change, space and time, causality, necessity and possibility.",
	}
	for _, text := range philTexts {
		entries = append(entries, DatasetEntry{Text: text, Source: "OpenPhilosophy", Domain: "Philosophy"})
	}

	return WriteJSONL(getPhaseFile("phase2_stem"), entries)
}

// RunPhase2Quantum handles practical Quantum Computing datasets (Clifford gates)
func RunPhase2Quantum() error {
	var entries []DatasetEntry

	quantumTexts := []string{
		"A Clifford gate is a quantum logic gate that belongs to the Clifford group. They are fundamental in quantum error correction.",
		"The Hadamard gate (H) is a single-qubit Clifford gate that creates a balanced superposition of the basis states.",
		"The Pauli-X, Y, and Z gates are basic quantum logic gates that are also part of the Clifford group.",
		"The CNOT gate (Controlled-NOT) is a two-qubit Clifford gate essential for creating entanglement.",
		"In stabilizer circuits, only Clifford gates are allowed, which can be simulated efficiently on a classical computer according to the Gottesman-Knill theorem.",
	}
	for _, text := range quantumTexts {
		entries = append(entries, DatasetEntry{Text: text, Source: "QuantumComputingResources", Domain: "Quantum/CliffordGates"})
	}

	return WriteJSONL(getPhaseFile("phase2_quantum"), entries)
}
