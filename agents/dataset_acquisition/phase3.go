package main

// RunPhase3CorporateWiki handles Corporate Documentation and Wikipedia Extracts
func RunPhase3CorporateWiki() error {
	var entries []DatasetEntry

	wikiTexts := []string{
		"STEM is a broad term used to group together these academic disciplines: science, technology, engineering, and mathematics.",
		"Scientific method is an empirical method of acquiring knowledge that has characterized the development of science since at least the 17th century.",
		"An encyclopedia or encyclopaedia is a reference work or compendium providing summaries of knowledge either from all branches or from a particular field or discipline.",
	}
	for _, text := range wikiTexts {
		entries = append(entries, DatasetEntry{Text: text, Source: "WikipediaExtracts", Domain: "Wikipedia/STEM"})
	}

	corpDocs := []string{
		"AWS (Amazon Web Services) offers a broad set of global cloud-based products including compute, storage, databases, analytics, networking, mobile, developer tools, management tools, IoT, security, and enterprise applications.",
		"Google Cloud Platform (GCP) is a suite of cloud computing services offered by Google. It runs on the same infrastructure that Google uses internally for its end-user products.",
		"Microsoft Azure, often referred to as Azure, is a cloud computing platform operated by Microsoft for application management via Microsoft-managed data centers.",
	}
	for _, text := range corpDocs {
		entries = append(entries, DatasetEntry{Text: text, Source: "CorporateDocs", Domain: "Corporate/Cloud"})
	}

	return WriteJSONL(getPhaseFile("phase3_wiki"), entries)
}
