<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <out>
      <!-- Show the oldest five deceased authors -->
      <xsl:for-each select="/authors/author">
        <xsl:sort select="life-span/died - life-span/born" order="descending"/>
        <xsl:sort select="life-span/born"/>
        <xsl:if test="position() &lt;= 5">
          <xsl:variable name="age" select="life-span/died - life-span/born"/>
          <oldest age="{$age}">
            <xsl:copy-of select="."/>
          </oldest>
        </xsl:if>
      </xsl:for-each>
    </out>
  </xsl:template>
</xsl:stylesheet>
