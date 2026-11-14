<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <!-- Two xsl:sort elements written directly (not via the "sort"
statement), each with explicit order and data-type attributes.
Primary key: whether the author has died, as a boolean cast to
text, descending, so "true" (deceased) sorts before "false"
(not known to be deceased).  Secondary key: birth year, as a
number, ascending, breaking ties within each group. -->
  <xsl:template match="/">
    <out>
      <xsl:for-each select="authors/author">
        <xsl:sort select="boolean(string(life-span/died))" order="descending" data-type="text"/>
        <xsl:sort select="life-span/born" order="ascending" data-type="number"/>
        <author last="{name/last}" first="{name/first}" born="{life-span/born}" died="{life-span/died}"/>
      </xsl:for-each>
    </out>
  </xsl:template>
</xsl:stylesheet>
