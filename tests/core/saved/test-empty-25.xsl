<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:redirect="org.apache.xalan.xslt.extensions.Redirect" version="1.0" extension-element-prefixes="redirect">
  <xsl:template match="/">
    <redirect:write append="true" href="foo.txt" method="text">
      <xsl:text>Hello, World!
</xsl:text>
    </redirect:write>
    <redirect:write append="true" href="foo.txt" method="text">
      <xsl:text>World goes Boom!
</xsl:text>
    </redirect:write>
  </xsl:template>
</xsl:stylesheet>
